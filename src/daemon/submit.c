#define _GNU_SOURCE

#include "submit.h"

#include <stdlib.h>
#include <errno.h>
#include <linux/ioprio.h>
#include <linux/io_uring.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

static unsigned load_acquire(const unsigned *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void store_release(unsigned *value, unsigned next)
{
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static int opcode_supported(const struct io_uring_probe *probe, uint8_t opcode)
{
    unsigned int index;

    for (index = 0; index < probe->ops_len; ++index) {
        if (probe->ops[index].op != opcode) {
            continue;
        }

        if ((probe->ops[index].flags & IO_URING_OP_SUPPORTED) != 0u) {
            return 1;
        }

        return 0;
    }

    return 0;
}

static uint32_t supported_submit_mask(uint32_t op_mask)
{
    return op_mask & IOURINGD_CAPABILITY_OP_MASK_V1;
}

static int iouringd_test_fault_matches(const char *value)
{
    const char *fault = getenv("IOURINGD_TEST_FAULT");

    return fault != NULL && strcmp(fault, value) == 0;
}

static int ring_setup_with_flags(unsigned entries,
                                 uint32_t flags,
                                 struct io_uring_params *params,
                                 int *ring_fd)
{
    memset(params, 0, sizeof(*params));
    params->flags = flags;
    if (iouringd_test_fault_matches("setup_enosys") != 0) {
        errno = ENOSYS;
        return -1;
    }
    if (iouringd_test_fault_matches("setup_eperm") != 0) {
        errno = EPERM;
        return -1;
    }
    *ring_fd = (int)syscall(__NR_io_uring_setup, entries, params);
    if (*ring_fd < 0) {
        return -1;
    }

    return 0;
}

static int ring_setup_best_effort(unsigned entries,
                                  struct io_uring_params *params,
                                  int *ring_fd,
                                  uint32_t *applied_flags)
{
    uint32_t attempts[3];
    size_t attempt_count = 0;
    size_t index;

#ifdef IORING_SETUP_SINGLE_ISSUER
    attempts[attempt_count] = IORING_SETUP_SINGLE_ISSUER;
    attempt_count += 1u;
#endif
    attempts[attempt_count] = 0u;
    attempt_count += 1u;

    for (index = 0; index < attempt_count; ++index) {
        if (ring_setup_with_flags(entries, attempts[index], params, ring_fd) == 0) {
            *applied_flags = attempts[index];
            return 0;
        }

        if (errno != EINVAL || index + 1u == attempt_count) {
            return -1;
        }
    }

    errno = EINVAL;
    return -1;
}

static uint16_t sqe_ioprio_from_submit_priority(uint16_t submit_priority)
{
    if (submit_priority == IOURINGD_SUBMIT_PRIORITY_HIGH) {
        return (uint16_t)IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 0);
    }

    if (submit_priority == IOURINGD_SUBMIT_PRIORITY_LOW) {
        return (uint16_t)IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 7);
    }

    return 0u;
}

static int ring_queue_sqe(struct iouringd_ring *ring,
                          uint8_t opcode,
                          iouringd_task_id_t task_id,
                          uint64_t addr,
                          uint32_t len,
                          uint16_t submit_priority)
{
    struct io_uring_sqe *sqe;
    unsigned sq_head;
    unsigned sq_tail;
    unsigned sq_index;

    if (ring == NULL || ring->ring_fd < 0) {
        errno = EINVAL;
        return -1;
    }
    (void)submit_priority;

    sq_head = load_acquire(ring->sq_head);
    sq_tail = *ring->sq_tail;
    if ((sq_tail - sq_head) >= *ring->sq_ring_entries) {
        errno = EBUSY;
        return -1;
    }

    sq_index = sq_tail & *ring->sq_ring_mask;
    sqe = &ring->sqes[sq_index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = opcode;
    sqe->user_data = task_id;
    sqe->addr = addr;
    sqe->len = len;
    ring->sq_array[sq_index] = sq_index;
    store_release(ring->sq_tail, sq_tail + 1u);

    if (syscall(__NR_io_uring_enter, ring->ring_fd, 1u, 0u, 0u, NULL) < 0) {
        return -1;
    }

    return 0;
}

static int ring_queue_rw_sqe(struct iouringd_ring *ring,
                             uint8_t opcode,
                             iouringd_task_id_t task_id,
                             unsigned file_index,
                             const struct iovec *iov,
                             unsigned iovcnt,
                             uint16_t submit_priority)
{
    struct io_uring_sqe *sqe;
    unsigned sq_head;
    unsigned sq_tail;
    unsigned sq_index;

    if (ring == NULL || ring->ring_fd < 0 || iov == NULL || iovcnt == 0u ||
        file_index >= ring->registered_files_count) {
        errno = EINVAL;
        return -1;
    }

    sq_head = load_acquire(ring->sq_head);
    sq_tail = *ring->sq_tail;
    if ((sq_tail - sq_head) >= *ring->sq_ring_entries) {
        errno = EBUSY;
        return -1;
    }

    sq_index = sq_tail & *ring->sq_ring_mask;
    sqe = &ring->sqes[sq_index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = opcode;
    sqe->user_data = task_id;
    sqe->fd = (int)file_index;
    sqe->flags = IOSQE_FIXED_FILE;
    sqe->addr = (uint64_t)(uintptr_t)iov;
    sqe->len = iovcnt;
    sqe->ioprio = sqe_ioprio_from_submit_priority(submit_priority);
    ring->sq_array[sq_index] = sq_index;
    store_release(ring->sq_tail, sq_tail + 1u);

    if (syscall(__NR_io_uring_enter, ring->ring_fd, 1u, 0u, 0u, NULL) < 0) {
        return -1;
    }

    return 0;
}

static int ring_queue_fixed_buffer_sqe(struct iouringd_ring *ring,
                                       uint8_t opcode,
                                       iouringd_task_id_t task_id,
                                       unsigned file_index,
                                       unsigned buffer_index,
                                       void *buf,
                                       unsigned len,
                                       uint16_t submit_priority)
{
    struct io_uring_sqe *sqe;
    unsigned sq_head;
    unsigned sq_tail;
    unsigned sq_index;

    if (ring == NULL || ring->ring_fd < 0 || buf == NULL ||
        file_index >= ring->registered_files_count ||
        buffer_index >= ring->registered_buffers_count) {
        errno = EINVAL;
        return -1;
    }

    sq_head = load_acquire(ring->sq_head);
    sq_tail = *ring->sq_tail;
    if ((sq_tail - sq_head) >= *ring->sq_ring_entries) {
        errno = EBUSY;
        return -1;
    }

    sq_index = sq_tail & *ring->sq_ring_mask;
    sqe = &ring->sqes[sq_index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = opcode;
    sqe->user_data = task_id;
    sqe->fd = (int)file_index;
    sqe->flags = IOSQE_FIXED_FILE;
    sqe->addr = (uint64_t)(uintptr_t)buf;
    sqe->len = len;
    sqe->buf_index = (uint16_t)buffer_index;
    sqe->ioprio = sqe_ioprio_from_submit_priority(submit_priority);
    ring->sq_array[sq_index] = sq_index;
    store_release(ring->sq_tail, sq_tail + 1u);

    if (syscall(__NR_io_uring_enter, ring->ring_fd, 1u, 0u, 0u, NULL) < 0) {
        return -1;
    }

    return 0;
}

static int ring_queue_poll_sqe(struct iouringd_ring *ring,
                               iouringd_task_id_t task_id,
                               unsigned file_index,
                               uint16_t poll_mask,
                               uint16_t submit_priority)
{
    struct io_uring_sqe *sqe;
    unsigned sq_head;
    unsigned sq_tail;
    unsigned sq_index;

    if (ring == NULL || ring->ring_fd < 0 || poll_mask == 0u ||
        file_index >= ring->registered_files_count) {
        errno = EINVAL;
        return -1;
    }
    (void)submit_priority;

    sq_head = load_acquire(ring->sq_head);
    sq_tail = *ring->sq_tail;
    if ((sq_tail - sq_head) >= *ring->sq_ring_entries) {
        errno = EBUSY;
        return -1;
    }

    sq_index = sq_tail & *ring->sq_ring_mask;
    sqe = &ring->sqes[sq_index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_POLL_ADD;
    sqe->user_data = task_id;
    sqe->fd = (int)file_index;
    sqe->flags = IOSQE_FIXED_FILE;
    sqe->poll_events = poll_mask;
    ring->sq_array[sq_index] = sq_index;
    store_release(ring->sq_tail, sq_tail + 1u);

    if (syscall(__NR_io_uring_enter, ring->ring_fd, 1u, 0u, 0u, NULL) < 0) {
        return -1;
    }

    return 0;
}

static int ring_queue_connect_sqe(struct iouringd_ring *ring,
                                  iouringd_task_id_t task_id,
                                  int fd,
                                  const void *sockaddr_buf,
                                  uint32_t sockaddr_length,
                                  uint16_t submit_priority)
{
    struct io_uring_sqe *sqe;
    unsigned sq_head;
    unsigned sq_tail;
    unsigned sq_index;

    if (ring == NULL || ring->ring_fd < 0 || sockaddr_buf == NULL ||
        sockaddr_length == 0u || sockaddr_length > UINT16_MAX || fd < 0) {
        errno = EINVAL;
        return -1;
    }
    (void)submit_priority;

    sq_head = load_acquire(ring->sq_head);
    sq_tail = *ring->sq_tail;
    if ((sq_tail - sq_head) >= *ring->sq_ring_entries) {
        errno = EBUSY;
        return -1;
    }

    sq_index = sq_tail & *ring->sq_ring_mask;
    sqe = &ring->sqes[sq_index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_CONNECT;
    sqe->user_data = task_id;
    sqe->fd = fd;
    sqe->addr = (uint64_t)(uintptr_t)sockaddr_buf;
    sqe->off = sockaddr_length;
    sqe->len = 0u;
    ring->sq_array[sq_index] = sq_index;
    store_release(ring->sq_tail, sq_tail + 1u);

    if (syscall(__NR_io_uring_enter, ring->ring_fd, 1u, 0u, 0u, NULL) < 0) {
        return -1;
    }

    return 0;
}

static int ring_queue_accept_sqe(struct iouringd_ring *ring,
                                 iouringd_task_id_t task_id,
                                 int fd,
                                 void *sockaddr_buf,
                                 socklen_t *sockaddr_length,
                                 unsigned accept_flags,
                                 uint16_t submit_priority)
{
    struct io_uring_sqe *sqe;
    unsigned sq_head;
    unsigned sq_tail;
    unsigned sq_index;

    if (ring == NULL || ring->ring_fd < 0 || fd < 0) {
        errno = EINVAL;
        return -1;
    }
    (void)submit_priority;

    if ((sockaddr_buf == NULL) != (sockaddr_length == NULL)) {
        errno = EINVAL;
        return -1;
    }

    sq_head = load_acquire(ring->sq_head);
    sq_tail = *ring->sq_tail;
    if ((sq_tail - sq_head) >= *ring->sq_ring_entries) {
        errno = EBUSY;
        return -1;
    }

    sq_index = sq_tail & *ring->sq_ring_mask;
    sqe = &ring->sqes[sq_index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_ACCEPT;
    sqe->user_data = task_id;
    sqe->fd = fd;
    sqe->addr = (uint64_t)(uintptr_t)sockaddr_buf;
    sqe->off = (uint64_t)(uintptr_t)sockaddr_length;
    sqe->accept_flags = accept_flags;
    ring->sq_array[sq_index] = sq_index;
    store_release(ring->sq_tail, sq_tail + 1u);

    if (syscall(__NR_io_uring_enter, ring->ring_fd, 1u, 0u, 0u, NULL) < 0) {
        return -1;
    }

    return 0;
}

static int ring_queue_openat_sqe(struct iouringd_ring *ring,
                                 iouringd_task_id_t task_id,
                                 int dirfd,
                                 const char *path,
                                 int32_t open_flags,
                                 uint32_t open_mode,
                                 uint16_t submit_priority)
{
    struct io_uring_sqe *sqe;
    unsigned sq_head;
    unsigned sq_tail;
    unsigned sq_index;

    if (ring == NULL || ring->ring_fd < 0 || path == NULL) {
        errno = EINVAL;
        return -1;
    }

    sq_head = load_acquire(ring->sq_head);
    sq_tail = *ring->sq_tail;
    if ((sq_tail - sq_head) >= *ring->sq_ring_entries) {
        errno = EBUSY;
        return -1;
    }

    sq_index = sq_tail & *ring->sq_ring_mask;
    sqe = &ring->sqes[sq_index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_OPENAT;
    sqe->user_data = task_id;
    sqe->fd = dirfd;
    sqe->addr = (uint64_t)(uintptr_t)path;
    sqe->open_flags = (uint32_t)open_flags;
    sqe->len = open_mode;
    sqe->ioprio = sqe_ioprio_from_submit_priority(submit_priority);
    ring->sq_array[sq_index] = sq_index;
    store_release(ring->sq_tail, sq_tail + 1u);

    if (syscall(__NR_io_uring_enter, ring->ring_fd, 1u, 0u, 0u, NULL) < 0) {
        return -1;
    }

    return 0;
}

static int ring_queue_close_sqe(struct iouringd_ring *ring,
                                iouringd_task_id_t task_id,
                                int fd,
                                uint16_t submit_priority)
{
    struct io_uring_sqe *sqe;
    unsigned sq_head;
    unsigned sq_tail;
    unsigned sq_index;

    if (ring == NULL || ring->ring_fd < 0 || fd < 0) {
        errno = EINVAL;
        return -1;
    }

    sq_head = load_acquire(ring->sq_head);
    sq_tail = *ring->sq_tail;
    if ((sq_tail - sq_head) >= *ring->sq_ring_entries) {
        errno = EBUSY;
        return -1;
    }

    sq_index = sq_tail & *ring->sq_ring_mask;
    sqe = &ring->sqes[sq_index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_CLOSE;
    sqe->user_data = task_id;
    sqe->fd = fd;
    sqe->ioprio = sqe_ioprio_from_submit_priority(submit_priority);
    ring->sq_array[sq_index] = sq_index;
    store_release(ring->sq_tail, sq_tail + 1u);

    if (syscall(__NR_io_uring_enter, ring->ring_fd, 1u, 0u, 0u, NULL) < 0) {
        return -1;
    }

    return 0;
}

int iouringd_ring_init(struct iouringd_ring *ring,
                       unsigned entries,
                       struct iouringd_capability_descriptor_v1 *capabilities)
{
    const unsigned int probe_ops_len = 256u;
    struct io_uring_params params;
    struct io_uring_probe *probe;
    size_t probe_size;
    uint32_t applied_setup_flags = 0u;
    uint32_t op_mask = 0;

    memset(ring, 0, sizeof(*ring));
    ring->ring_fd = -1;
    if (entries == 0u) {
        errno = EINVAL;
        return -1;
    }

    if (ring_setup_best_effort(entries,
                               &params,
                               &ring->ring_fd,
                               &applied_setup_flags) != 0) {
        return -1;
    }
    ring->setup_flags = applied_setup_flags;

    ring->sq_ring_size = params.sq_off.array +
                         params.sq_entries * sizeof(ring->sq_array[0]);
    ring->cq_ring_size = params.cq_off.cqes +
                         params.cq_entries * sizeof(ring->cqes[0]);
    ring->sqes_size = params.sq_entries * sizeof(*ring->sqes);

    if ((params.features & IORING_FEAT_SINGLE_MMAP) != 0u) {
        if (ring->cq_ring_size > ring->sq_ring_size) {
            ring->sq_ring_size = ring->cq_ring_size;
        } else {
            ring->cq_ring_size = ring->sq_ring_size;
        }
    }

    ring->sq_ring_ptr = mmap(NULL,
                             ring->sq_ring_size,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED,
                             ring->ring_fd,
                             IORING_OFF_SQ_RING);
    if (ring->sq_ring_ptr == MAP_FAILED) {
        ring->sq_ring_ptr = NULL;
        close(ring->ring_fd);
        ring->ring_fd = -1;
        return -1;
    }

    if ((params.features & IORING_FEAT_SINGLE_MMAP) != 0u) {
        ring->cq_ring_ptr = ring->sq_ring_ptr;
    } else {
        ring->cq_ring_ptr = mmap(NULL,
                                 ring->cq_ring_size,
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED,
                                 ring->ring_fd,
                                 IORING_OFF_CQ_RING);
        if (ring->cq_ring_ptr == MAP_FAILED) {
            munmap(ring->sq_ring_ptr, ring->sq_ring_size);
            ring->sq_ring_ptr = NULL;
            ring->cq_ring_ptr = NULL;
            close(ring->ring_fd);
            ring->ring_fd = -1;
            return -1;
        }
    }

    ring->sqes = mmap(NULL,
                      ring->sqes_size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      ring->ring_fd,
                      IORING_OFF_SQES);
    if (ring->sqes == MAP_FAILED) {
        ring->sqes = NULL;
        if (ring->cq_ring_ptr != NULL && ring->cq_ring_ptr != ring->sq_ring_ptr) {
            munmap(ring->cq_ring_ptr, ring->cq_ring_size);
        }
        munmap(ring->sq_ring_ptr, ring->sq_ring_size);
        ring->sq_ring_ptr = NULL;
        ring->cq_ring_ptr = NULL;
        close(ring->ring_fd);
        ring->ring_fd = -1;
        return -1;
    }

    ring->sq_head = (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.head);
    ring->sq_tail = (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.tail);
    ring->sq_ring_mask = (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.ring_mask);
    ring->sq_ring_entries = (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.ring_entries);
    ring->sq_array = (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.array);
    ring->cq_head = (unsigned *)((char *)ring->cq_ring_ptr + params.cq_off.head);
    ring->cq_tail = (unsigned *)((char *)ring->cq_ring_ptr + params.cq_off.tail);
    ring->cq_ring_mask = (unsigned *)((char *)ring->cq_ring_ptr + params.cq_off.ring_mask);
    ring->cqes = (struct io_uring_cqe *)((char *)ring->cq_ring_ptr + params.cq_off.cqes);

    if (capabilities == NULL) {
        return 0;
    }

    probe_size = sizeof(*probe) + (size_t)probe_ops_len * sizeof(probe->ops[0]);
    probe = mmap(NULL,
                 probe_size,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1,
                 0);
    if (probe == MAP_FAILED) {
        iouringd_ring_cleanup(ring);
        return -1;
    }

    memset(probe, 0, probe_size);
    probe->ops_len = 0u;
    if (iouringd_test_fault_matches("probe_eperm") != 0) {
        int saved_errno = EPERM;

        munmap(probe, probe_size);
        iouringd_ring_cleanup(ring);
        errno = saved_errno;
        return -1;
    }
    if (syscall(__NR_io_uring_register,
                ring->ring_fd,
                IORING_REGISTER_PROBE,
                probe,
                probe_ops_len) != 0) {
        int saved_errno = errno;

        munmap(probe, probe_size);
        iouringd_ring_cleanup(ring);
        errno = saved_errno;
        return -1;
    }

    if (opcode_supported(probe, IORING_OP_NOP) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_NOP;
    }

    if (opcode_supported(probe, IORING_OP_TIMEOUT) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_TIMEOUT;
    }

    if (opcode_supported(probe, IORING_OP_READV) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_READV;
    }

    if (opcode_supported(probe, IORING_OP_WRITEV) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_WRITEV;
    }

    if (opcode_supported(probe, IORING_OP_ASYNC_CANCEL) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_ASYNC_CANCEL;
    }

    if (opcode_supported(probe, IORING_OP_READ_FIXED) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_READ_FIXED;
    }

    if (opcode_supported(probe, IORING_OP_WRITE_FIXED) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_WRITE_FIXED;
    }

    if (opcode_supported(probe, IORING_OP_POLL_ADD) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_POLL;
    }

    if (opcode_supported(probe, IORING_OP_CONNECT) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_CONNECT;
    }

    if (opcode_supported(probe, IORING_OP_ACCEPT) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_ACCEPT;
    }

    if (opcode_supported(probe, IORING_OP_OPENAT) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_OPENAT;
    }

    if (opcode_supported(probe, IORING_OP_CLOSE) != 0) {
        op_mask |= IOURINGD_CAPABILITY_OP_CLOSE;
    }

    munmap(probe, probe_size);

    op_mask = supported_submit_mask(op_mask);
    if (op_mask == 0u) {
        iouringd_ring_cleanup(ring);
        errno = ENOTSUP;
        return -1;
    }

    memset(capabilities, 0, sizeof(*capabilities));
    capabilities->struct_size = (uint32_t)sizeof(*capabilities);
    capabilities->features = params.features;
    capabilities->ring_entries = params.sq_entries;
    capabilities->cq_entries = params.cq_entries;
    capabilities->op_mask = op_mask;
    capabilities->submit_credits = params.sq_entries > 1u ? params.sq_entries - 1u : 0u;
    return 0;
}

int iouringd_ring_init_registered_files(struct iouringd_ring *ring, unsigned count)
{
    int *files;
    unsigned index;
    int rc;
    int saved_errno;

    if (ring == NULL || ring->ring_fd < 0 || count == 0u) {
        errno = EINVAL;
        return -1;
    }

    files = mmap(NULL,
                 (size_t)count * sizeof(*files),
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1,
                 0);
    if (files == MAP_FAILED) {
        return -1;
    }

    for (index = 0; index < count; ++index) {
        files[index] = -1;
    }

    rc = (int)syscall(__NR_io_uring_register,
                      ring->ring_fd,
                      IORING_REGISTER_FILES,
                      files,
                      count);
    saved_errno = errno;
    munmap(files, (size_t)count * sizeof(*files));
    errno = saved_errno;
    if (rc != 0) {
        return -1;
    }

    ring->registered_files_count = count;
    return 0;
}

int iouringd_ring_init_registered_buffers(struct iouringd_ring *ring,
                                          const struct iovec *iovecs,
                                          unsigned count)
{
    int rc;

    if (ring == NULL || ring->ring_fd < 0 || iovecs == NULL || count == 0u) {
        errno = EINVAL;
        return -1;
    }

    rc = (int)syscall(__NR_io_uring_register,
                      ring->ring_fd,
                      IORING_REGISTER_BUFFERS,
                      iovecs,
                      count);
    if (rc != 0) {
        return -1;
    }

    ring->registered_buffers_count = count;
    return 0;
}

int iouringd_ring_update_registered_file(struct iouringd_ring *ring,
                                         unsigned index,
                                         int fd)
{
    struct io_uring_files_update update;
    int update_fd;
    int rc;

    if (ring == NULL || ring->ring_fd < 0 || index >= ring->registered_files_count ||
        fd < -1) {
        errno = EINVAL;
        return -1;
    }

    update_fd = fd;
    memset(&update, 0, sizeof(update));
    update.offset = index;
    update.fds = (uint64_t)(uintptr_t)&update_fd;
    rc = (int)syscall(__NR_io_uring_register,
                      ring->ring_fd,
                      IORING_REGISTER_FILES_UPDATE,
                      &update,
                      1u);
    if (rc < 0) {
        return -1;
    }
    if (rc != 1) {
        errno = EIO;
        return -1;
    }

    return 0;
}

void iouringd_ring_cleanup(struct iouringd_ring *ring)
{
    if (ring->sqes != NULL) {
        munmap(ring->sqes, ring->sqes_size);
        ring->sqes = NULL;
    }

    if (ring->cq_ring_ptr != NULL && ring->cq_ring_ptr != ring->sq_ring_ptr) {
        munmap(ring->cq_ring_ptr, ring->cq_ring_size);
    }
    ring->cq_ring_ptr = NULL;

    if (ring->sq_ring_ptr != NULL) {
        munmap(ring->sq_ring_ptr, ring->sq_ring_size);
        ring->sq_ring_ptr = NULL;
    }

    if (ring->ring_fd >= 0) {
        close(ring->ring_fd);
        ring->ring_fd = -1;
    }
}

unsigned iouringd_ring_capacity(const struct iouringd_ring *ring)
{
    if (ring == NULL || ring->sq_ring_entries == NULL) {
        return 0u;
    }

    return *ring->sq_ring_entries;
}

int iouringd_ring_submit_nop(struct iouringd_ring *ring,
                             iouringd_task_id_t task_id,
                             uint16_t submit_priority)
{
    return ring_queue_sqe(ring, IORING_OP_NOP, task_id, 0u, 0u, submit_priority);
}

int iouringd_ring_submit_timeout(
    struct iouringd_ring *ring,
    iouringd_task_id_t task_id,
    const struct __kernel_timespec *timeout,
    uint16_t submit_priority)
{
    if (timeout == NULL) {
        errno = EINVAL;
        return -1;
    }

    return ring_queue_sqe(ring,
                          IORING_OP_TIMEOUT,
                          task_id,
                          (uint64_t)(uintptr_t)timeout,
                          1u,
                          submit_priority);
}

int iouringd_ring_submit_cancel(struct iouringd_ring *ring,
                                iouringd_task_id_t task_id,
                                iouringd_task_id_t target_task_id,
                                uint16_t submit_priority)
{
    struct io_uring_sqe *sqe;
    unsigned sq_head;
    unsigned sq_tail;
    unsigned sq_index;

    if (ring == NULL || ring->ring_fd < 0 ||
        target_task_id == IOURINGD_TASK_ID_INVALID ||
        target_task_id >= IOURINGD_TASK_ID_RESERVED_MIN) {
        errno = EINVAL;
        return -1;
    }
    (void)submit_priority;

    sq_head = load_acquire(ring->sq_head);
    sq_tail = *ring->sq_tail;
    if ((sq_tail - sq_head) >= *ring->sq_ring_entries) {
        errno = EBUSY;
        return -1;
    }

    sq_index = sq_tail & *ring->sq_ring_mask;
    sqe = &ring->sqes[sq_index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_ASYNC_CANCEL;
    sqe->user_data = task_id;
    sqe->addr = (uint64_t)target_task_id;
    sqe->cancel_flags = IORING_ASYNC_CANCEL_USERDATA;
    ring->sq_array[sq_index] = sq_index;
    store_release(ring->sq_tail, sq_tail + 1u);

    if (syscall(__NR_io_uring_enter, ring->ring_fd, 1u, 0u, 0u, NULL) < 0) {
        return -1;
    }

    return 0;
}

int iouringd_ring_submit_readv(struct iouringd_ring *ring,
                               iouringd_task_id_t task_id,
                               unsigned file_index,
                               const struct iovec *iov,
                               unsigned iovcnt,
                               uint16_t submit_priority)
{
    return ring_queue_rw_sqe(ring,
                             IORING_OP_READV,
                             task_id,
                             file_index,
                             iov,
                             iovcnt,
                             submit_priority);
}

int iouringd_ring_submit_writev(struct iouringd_ring *ring,
                                iouringd_task_id_t task_id,
                                unsigned file_index,
                                const struct iovec *iov,
                                unsigned iovcnt,
                                uint16_t submit_priority)
{
    return ring_queue_rw_sqe(ring,
                             IORING_OP_WRITEV,
                             task_id,
                             file_index,
                             iov,
                             iovcnt,
                             submit_priority);
}

int iouringd_ring_submit_poll(struct iouringd_ring *ring,
                              iouringd_task_id_t task_id,
                              unsigned file_index,
                              uint16_t poll_mask,
                              uint16_t submit_priority)
{
    return ring_queue_poll_sqe(ring, task_id, file_index, poll_mask, submit_priority);
}

int iouringd_ring_submit_connect(struct iouringd_ring *ring,
                                 iouringd_task_id_t task_id,
                                 int fd,
                                 const void *sockaddr_buf,
                                 uint32_t sockaddr_length,
                                 uint16_t submit_priority)
{
    return ring_queue_connect_sqe(ring,
                                  task_id,
                                  fd,
                                  sockaddr_buf,
                                  sockaddr_length,
                                  submit_priority);
}

int iouringd_ring_submit_accept(struct iouringd_ring *ring,
                                iouringd_task_id_t task_id,
                                int fd,
                                void *sockaddr_buf,
                                socklen_t *sockaddr_length,
                                unsigned accept_flags,
                                uint16_t submit_priority)
{
    return ring_queue_accept_sqe(ring,
                                 task_id,
                                 fd,
                                 sockaddr_buf,
                                 sockaddr_length,
                                 accept_flags,
                                 submit_priority);
}

int iouringd_ring_submit_openat(struct iouringd_ring *ring,
                                iouringd_task_id_t task_id,
                                int dirfd,
                                const char *path,
                                int32_t open_flags,
                                uint32_t open_mode,
                                uint16_t submit_priority)
{
    return ring_queue_openat_sqe(ring,
                                 task_id,
                                 dirfd,
                                 path,
                                 open_flags,
                                 open_mode,
                                 submit_priority);
}

int iouringd_ring_submit_close(struct iouringd_ring *ring,
                               iouringd_task_id_t task_id,
                               int fd,
                               uint16_t submit_priority)
{
    return ring_queue_close_sqe(ring, task_id, fd, submit_priority);
}

int iouringd_ring_submit_read_fixed(struct iouringd_ring *ring,
                                    iouringd_task_id_t task_id,
                                    unsigned file_index,
                                    unsigned buffer_index,
                                    void *buf,
                                    unsigned len,
                                    uint16_t submit_priority)
{
    return ring_queue_fixed_buffer_sqe(ring,
                                       IORING_OP_READ_FIXED,
                                       task_id,
                                       file_index,
                                       buffer_index,
                                       buf,
                                       len,
                                       submit_priority);
}

int iouringd_ring_submit_write_fixed(struct iouringd_ring *ring,
                                     iouringd_task_id_t task_id,
                                     unsigned file_index,
                                     unsigned buffer_index,
                                     const void *buf,
                                     unsigned len,
                                     uint16_t submit_priority)
{
    return ring_queue_fixed_buffer_sqe(ring,
                                       IORING_OP_WRITE_FIXED,
                                       task_id,
                                       file_index,
                                       buffer_index,
                                       (void *)buf,
                                       len,
                                       submit_priority);
}

int iouringd_ring_peek_completion(struct iouringd_ring *ring,
                                  iouringd_task_id_t *task_id,
                                  int32_t *res,
                                  int *available)
{
    const struct io_uring_cqe *cqe;
    unsigned cq_head;

    if (ring == NULL || task_id == NULL || res == NULL || available == NULL) {
        errno = EINVAL;
        return -1;
    }

    cq_head = load_acquire(ring->cq_head);
    if (cq_head == load_acquire(ring->cq_tail)) {
        *available = 0;
        return 0;
    }

    cqe = &ring->cqes[cq_head & *ring->cq_ring_mask];
    *task_id = (iouringd_task_id_t)cqe->user_data;
    *res = cqe->res;
    *available = 1;
    store_release(ring->cq_head, cq_head + 1u);
    return 0;
}
