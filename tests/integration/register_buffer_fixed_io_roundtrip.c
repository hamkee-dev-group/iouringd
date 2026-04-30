#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
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

int main(int argc, char **argv)
{
    static const char message[] = "buffered-data";
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_id_record_v1 buffer_resource;
    struct iouringd_resource_id_record_v1 read_resource;
    struct iouringd_resource_id_record_v1 write_resource;
    struct iouringd_task_id_record_v1 read_task;
    struct iouringd_task_id_record_v1 write_task;
    char payload[sizeof(message)];
    char peer_buf[sizeof(message)];
    char socket_path[108];
    size_t payload_length;
    pid_t child;
    int control_fd;
    int read_fds[2];
    int write_fds[2];
    int status;

    if (argc != 2) {
        return 2;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, read_fds) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, write_fds) != 0) {
        return 1;
    }
    if (set_cloexec(read_fds[0]) != 0 || set_cloexec(read_fds[1]) != 0 ||
        set_cloexec(write_fds[0]) != 0 || set_cloexec(write_fds[1]) != 0) {
        close(read_fds[0]);
        close(read_fds[1]);
        close(write_fds[0]);
        close(write_fds[1]);
        return 1;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-buffer-fixed-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    child = fork();
    if (child < 0) {
        close(read_fds[0]);
        close(read_fds[1]);
        close(write_fds[0]);
        close(write_fds[1]);
        return 1;
    }

    if (child == 0) {
        execl(argv[1], argv[1], socket_path, (char *)NULL);
        _exit(127);
    }

    control_fd = wait_for_connect(socket_path, child);
    if (control_fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[0]);
        close(read_fds[1]);
        close(write_fds[0]);
        close(write_fds[1]);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_READ_FIXED) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_WRITE_FIXED) == 0u ||
        handshake.capabilities.registered_fd_slots == 0u ||
        handshake.capabilities.registered_buffer_slots == 0u ||
        handshake.capabilities.io_bytes_max < sizeof(message) - 1u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[0]);
        close(read_fds[1]);
        close(write_fds[0]);
        close(write_fds[1]);
        return 1;
    }

    memset(&read_resource, 0, sizeof(read_resource));
    if (iouringd_register_fd(control_fd, read_fds[0], &read_resource) != 0 ||
        read_resource.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[0]);
        close(read_fds[1]);
        close(write_fds[0]);
        close(write_fds[1]);
        return 1;
    }
    close(read_fds[0]);
    read_fds[0] = -1;

    memset(&write_resource, 0, sizeof(write_resource));
    if (iouringd_register_fd(control_fd, write_fds[0], &write_resource) != 0 ||
        write_resource.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[1]);
        close(write_fds[0]);
        close(write_fds[1]);
        return 1;
    }
    close(write_fds[0]);
    write_fds[0] = -1;

    memset(&buffer_resource, 0, sizeof(buffer_resource));
    if (iouringd_register_buffer(control_fd, NULL, 0u, &buffer_resource) != 0 ||
        buffer_resource.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[1]);
        close(write_fds[1]);
        return 1;
    }

    memset(&read_task, 0, sizeof(read_task));
    if (iouringd_submit_sock_read_fixed(control_fd,
                                        read_resource.resource_id,
                                        buffer_resource.resource_id,
                                        (uint32_t)(sizeof(message) - 1u),
                                        &read_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[1]);
        close(write_fds[1]);
        return 1;
    }

    if (write_full(read_fds[1], message, sizeof(message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[1]);
        close(write_fds[1]);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != read_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_SOCK_READ_FIXED ||
        completion.res != (int32_t)(sizeof(message) - 1u) ||
        completion.payload_length != sizeof(message) - 1u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[1]);
        close(write_fds[1]);
        return 1;
    }

    memset(payload, 0, sizeof(payload));
    payload_length = 0u;
    if (iouringd_read_completion_payload(control_fd,
                                         &completion,
                                         payload,
                                         sizeof(payload),
                                         &payload_length) != 0 ||
        payload_length != sizeof(message) - 1u ||
        memcmp(payload, message, sizeof(message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[1]);
        close(write_fds[1]);
        return 1;
    }

    memset(&write_task, 0, sizeof(write_task));
    if (iouringd_submit_sock_write_fixed(control_fd,
                                         write_resource.resource_id,
                                         buffer_resource.resource_id,
                                         (uint32_t)(sizeof(message) - 1u),
                                         &write_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[1]);
        close(write_fds[1]);
        return 1;
    }

    memset(peer_buf, 0, sizeof(peer_buf));
    if (read_full(write_fds[1], peer_buf, sizeof(message) - 1u) != 0 ||
        memcmp(peer_buf, message, sizeof(message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[1]);
        close(write_fds[1]);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != write_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_SOCK_WRITE_FIXED ||
        completion.res != (int32_t)(sizeof(message) - 1u) ||
        completion.payload_length != 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(read_fds[1]);
        close(write_fds[1]);
        return 1;
    }

    close(control_fd);
    close(read_fds[1]);
    close(write_fds[1]);

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
