#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "iouringd/client.h"
#include "iouringd/submit.h"

static int wait_for_connect(const char *socket_path, pid_t child)
{
    struct timespec delay;
    int attempt;

    delay.tv_sec = 0;
    delay.tv_nsec = 10000000L;

    for (attempt = 0; attempt < 100; ++attempt) {
        int fd;
        int status;
        pid_t rc = waitpid(child, &status, WNOHANG);

        if (rc == child) {
            return -1;
        }

        fd = iouringd_client_connect(socket_path);
        if (fd >= 0) {
            return fd;
        }

        nanosleep(&delay, NULL);
    }

    return -1;
}

static int write_full(int fd, const void *buf, size_t len)
{
    size_t offset = 0;
    const unsigned char *bytes = buf;

    while (offset < len) {
        ssize_t rc = write(fd, bytes + offset, len - offset);

        if (rc <= 0) {
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

static int read_full(int fd, void *buf, size_t len)
{
    size_t offset = 0;
    unsigned char *bytes = buf;

    while (offset < len) {
        ssize_t rc = read(fd, bytes + offset, len - offset);

        if (rc <= 0) {
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

static int set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);

    if (flags < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int stranded_task_is_tracked(int control_fd,
                                    const struct iouringd_task_id_record_v1 *task,
                                    uint64_t expected_accepted)
{
    struct iouringd_stats_result_v1 stats;

    if (task->task_id == IOURINGD_TASK_ID_INVALID) {
        return -1;
    }

    memset(&stats, 0, sizeof(stats));
    if (iouringd_get_stats(control_fd, &stats) != 0 ||
        stats.outstanding_tasks != 1u ||
        stats.outstanding_credit_tasks != 1u ||
        stats.accepted_submits != expected_accepted ||
        stats.rejected_submits != 0u) {
        return -1;
    }

    return 0;
}

static int flush_and_collect(int control_fd,
                             struct iouringd_completion_record_v1 *records,
                             struct iouringd_task_id_record_v1 *nop_task)
{
    size_t index;

    memset(nop_task, 0, sizeof(*nop_task));
    if (iouringd_submit_nop(control_fd, nop_task) != 0 ||
        nop_task->task_id == IOURINGD_TASK_ID_INVALID) {
        return -1;
    }

    for (index = 0; index < 2u; ++index) {
        memset(&records[index], 0, sizeof(records[index]));
        if (iouringd_wait_completion(control_fd, &records[index]) != 0) {
            return -1;
        }
    }

    return 0;
}

static const struct iouringd_completion_record_v1 *find_completion(
    const struct iouringd_completion_record_v1 *records,
    iouringd_task_id_t task_id)
{
    size_t index;

    for (index = 0; index < 2u; ++index) {
        if (records[index].task.task_id == task_id) {
            return &records[index];
        }
    }

    return NULL;
}

static int nop_completed(const struct iouringd_completion_record_v1 *records,
                         const struct iouringd_task_id_record_v1 *nop_task)
{
    const struct iouringd_completion_record_v1 *matched =
        find_completion(records, nop_task->task_id);

    if (matched == NULL || matched->task_kind.value != IOURINGD_TASK_KIND_NOP ||
        matched->res != IOURINGD_COMPLETION_RES_OK) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    static const char write_message[] = "published-write";
    static const char read_message[] = "published-read";
    struct iouringd_completion_record_v1 records[2];
    const struct iouringd_completion_record_v1 *matched;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_id_record_v1 resource;
    struct iouringd_stats_result_v1 stats;
    struct iouringd_task_id_record_v1 nop_task;
    struct iouringd_task_id_record_v1 op_task;
    char payload[sizeof(read_message)];
    char peer_buf[sizeof(write_message)];
    char socket_path[108];
    size_t payload_length;
    pid_t child;
    int control_fd;
    int sp[2];
    int status;

    if (argc != 2) {
        return 2;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
        return 1;
    }
    if (set_cloexec(sp[0]) != 0 || set_cloexec(sp[1]) != 0) {
        close(sp[0]);
        close(sp[1]);
        return 1;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-enter-publish-rw-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    if (setenv("IOURINGD_TEST_FAULT", "enter_after_publish", 1) != 0) {
        close(sp[0]);
        close(sp[1]);
        return 1;
    }

    child = fork();
    if (child < 0) {
        unsetenv("IOURINGD_TEST_FAULT");
        close(sp[0]);
        close(sp[1]);
        return 1;
    }

    if (child == 0) {
        execl(argv[1], argv[1], socket_path, (char *)NULL);
        _exit(127);
    }
    unsetenv("IOURINGD_TEST_FAULT");

    control_fd = wait_for_connect(socket_path, child);
    if (control_fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[0]);
        close(sp[1]);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_READV) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_WRITEV) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_POLL) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_NOP) == 0u ||
        handshake.capabilities.registered_fd_slots == 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[0]);
        close(sp[1]);
        return 1;
    }

    memset(&resource, 0, sizeof(resource));
    if (iouringd_register_fd(control_fd, sp[0], &resource) != 0 ||
        resource.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[0]);
        close(sp[1]);
        return 1;
    }
    close(sp[0]);
    sp[0] = -1;

    memset(&op_task, 0, sizeof(op_task));
    if (iouringd_submit_sock_write(control_fd,
                                   resource.resource_id,
                                   write_message,
                                   (uint32_t)(sizeof(write_message) - 1u),
                                   &op_task) != 0 ||
        stranded_task_is_tracked(control_fd, &op_task, UINT64_C(1)) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    if (flush_and_collect(control_fd, records, &nop_task) != 0 ||
        nop_completed(records, &nop_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    matched = find_completion(records, op_task.task_id);
    if (matched == NULL ||
        matched->task_kind.value != IOURINGD_TASK_KIND_SOCK_WRITE ||
        matched->res != (int32_t)(sizeof(write_message) - 1u)) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    memset(peer_buf, 0, sizeof(peer_buf));
    if (read_full(sp[1], peer_buf, sizeof(write_message) - 1u) != 0 ||
        memcmp(peer_buf, write_message, sizeof(write_message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    if (write_full(sp[1], read_message, sizeof(read_message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    memset(&op_task, 0, sizeof(op_task));
    if (iouringd_submit_poll(control_fd, resource.resource_id, POLLIN, &op_task) != 0 ||
        stranded_task_is_tracked(control_fd, &op_task, UINT64_C(3)) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    if (flush_and_collect(control_fd, records, &nop_task) != 0 ||
        nop_completed(records, &nop_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    matched = find_completion(records, op_task.task_id);
    if (matched == NULL || matched->task_kind.value != IOURINGD_TASK_KIND_POLL ||
        matched->res < 0 || (matched->res & POLLIN) == 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    memset(&op_task, 0, sizeof(op_task));
    if (iouringd_submit_sock_read(control_fd,
                                  resource.resource_id,
                                  (uint32_t)(sizeof(read_message) - 1u),
                                  &op_task) != 0 ||
        stranded_task_is_tracked(control_fd, &op_task, UINT64_C(5)) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    if (flush_and_collect(control_fd, records, &nop_task) != 0 ||
        nop_completed(records, &nop_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    matched = find_completion(records, op_task.task_id);
    if (matched == NULL ||
        matched->task_kind.value != IOURINGD_TASK_KIND_SOCK_READ ||
        matched->res != (int32_t)(sizeof(read_message) - 1u) ||
        matched->payload_length != sizeof(read_message) - 1u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    memset(payload, 0, sizeof(payload));
    payload_length = 0u;
    if (iouringd_read_completion_payload(control_fd,
                                         matched,
                                         payload,
                                         sizeof(payload),
                                         &payload_length) != 0 ||
        payload_length != sizeof(read_message) - 1u ||
        memcmp(payload, read_message, sizeof(read_message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    memset(&stats, 0, sizeof(stats));
    if (iouringd_get_stats(control_fd, &stats) != 0 ||
        stats.outstanding_tasks != 0u ||
        stats.outstanding_credit_tasks != 0u ||
        stats.accepted_submits != UINT64_C(6) ||
        stats.rejected_submits != 0u ||
        stats.completions != UINT64_C(6)) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    if (iouringd_release_resource(control_fd, resource.resource_id, &resource) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(sp[1]);
        return 1;
    }

    close(control_fd);
    close(sp[1]);

    kill(child, SIGTERM);
    if (waitpid(child, &status, 0) != child) {
        unlink(socket_path);
        return 1;
    }

    unlink(socket_path);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        return 1;
    }

    return 0;
}
