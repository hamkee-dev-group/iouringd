#ifndef IOURINGD_DAEMON_SUBMIT_H
#define IOURINGD_DAEMON_SUBMIT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <linux/time_types.h>

#include "iouringd/api.h"

struct io_uring_cqe;
struct io_uring_sqe;

struct iouringd_ring {
    int ring_fd;
    uint32_t setup_flags;
    unsigned registered_files_count;
    unsigned registered_buffers_count;
    void *sq_ring_ptr;
    void *cq_ring_ptr;
    struct io_uring_sqe *sqes;
    size_t sq_ring_size;
    size_t cq_ring_size;
    size_t sqes_size;
    unsigned *sq_head;
    unsigned *sq_tail;
    unsigned *sq_ring_mask;
    unsigned *sq_ring_entries;
    unsigned *sq_array;
    unsigned *cq_head;
    unsigned *cq_tail;
    unsigned *cq_ring_mask;
    struct io_uring_cqe *cqes;
};

int iouringd_ring_init(struct iouringd_ring *ring,
                       unsigned entries,
                       struct iouringd_capability_descriptor_v1 *capabilities);
int iouringd_ring_init_registered_files(struct iouringd_ring *ring, unsigned count);
int iouringd_ring_init_registered_buffers(struct iouringd_ring *ring,
                                          const struct iovec *iovecs,
                                          unsigned count);
int iouringd_ring_update_registered_file(struct iouringd_ring *ring,
                                         unsigned index,
                                         int fd);
void iouringd_ring_cleanup(struct iouringd_ring *ring);
unsigned iouringd_ring_capacity(const struct iouringd_ring *ring);
int iouringd_ring_submit_nop(struct iouringd_ring *ring,
                             iouringd_task_id_t task_id,
                             uint16_t submit_priority);
int iouringd_ring_submit_timeout(
    struct iouringd_ring *ring,
    iouringd_task_id_t task_id,
    const struct __kernel_timespec *timeout,
    uint16_t submit_priority);
int iouringd_ring_submit_cancel(struct iouringd_ring *ring,
                                iouringd_task_id_t task_id,
                                iouringd_task_id_t target_task_id,
                                uint16_t submit_priority);
int iouringd_ring_submit_readv(struct iouringd_ring *ring,
                               iouringd_task_id_t task_id,
                               unsigned file_index,
                               const struct iovec *iov,
                               unsigned iovcnt,
                               uint16_t submit_priority);
int iouringd_ring_submit_writev(struct iouringd_ring *ring,
                                iouringd_task_id_t task_id,
                                unsigned file_index,
                                const struct iovec *iov,
                                unsigned iovcnt,
                                uint16_t submit_priority);
int iouringd_ring_submit_poll(struct iouringd_ring *ring,
                              iouringd_task_id_t task_id,
                              unsigned file_index,
                              uint16_t poll_mask,
                              uint16_t submit_priority);
int iouringd_ring_submit_connect(struct iouringd_ring *ring,
                                 iouringd_task_id_t task_id,
                                 int fd,
                                 const void *sockaddr_buf,
                                 uint32_t sockaddr_length,
                                 uint16_t submit_priority);
int iouringd_ring_submit_accept(struct iouringd_ring *ring,
                                iouringd_task_id_t task_id,
                                int fd,
                                void *sockaddr_buf,
                                socklen_t *sockaddr_length,
                                unsigned accept_flags,
                                uint16_t submit_priority);
int iouringd_ring_submit_openat(struct iouringd_ring *ring,
                                iouringd_task_id_t task_id,
                                int dirfd,
                                const char *path,
                                int32_t open_flags,
                                uint32_t open_mode,
                                uint16_t submit_priority);
int iouringd_ring_submit_close(struct iouringd_ring *ring,
                               iouringd_task_id_t task_id,
                               int fd,
                               uint16_t submit_priority);
int iouringd_ring_submit_read_fixed(struct iouringd_ring *ring,
                                    iouringd_task_id_t task_id,
                                    unsigned file_index,
                                    unsigned buffer_index,
                                    void *buf,
                                    unsigned len,
                                    uint16_t submit_priority);
int iouringd_ring_submit_write_fixed(struct iouringd_ring *ring,
                                     iouringd_task_id_t task_id,
                                     unsigned file_index,
                                     unsigned buffer_index,
                                     const void *buf,
                                     unsigned len,
                                     uint16_t submit_priority);
int iouringd_ring_peek_completion(struct iouringd_ring *ring,
                                  iouringd_task_id_t *task_id,
                                  int32_t *res,
                                  int *available);

#endif
