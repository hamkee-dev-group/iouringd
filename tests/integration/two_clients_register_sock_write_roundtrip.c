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
    static const char message_a[] = "client-a";
    static const char message_b[] = "client-b";
    struct iouringd_completion_record_v1 completion_a;
    struct iouringd_completion_record_v1 completion_b;
    struct iouringd_handshake_result_v1 handshake_a;
    struct iouringd_handshake_result_v1 handshake_b;
    struct iouringd_resource_id_record_v1 resource_a;
    struct iouringd_resource_id_record_v1 resource_b;
    struct iouringd_task_id_record_v1 task_a;
    struct iouringd_task_id_record_v1 task_b;
    char peer_a[sizeof(message_a)];
    char peer_b[sizeof(message_b)];
    char socket_path[108];
    pid_t child;
    int control_a;
    int control_b;
    int io_a[2];
    int io_b[2];
    int status;

    if (argc != 2) {
        return 2;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, io_a) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, io_b) != 0) {
        return 1;
    }
    if (set_cloexec(io_a[0]) != 0 || set_cloexec(io_a[1]) != 0 ||
        set_cloexec(io_b[0]) != 0 || set_cloexec(io_b[1]) != 0) {
        close(io_a[0]);
        close(io_a[1]);
        close(io_b[0]);
        close(io_b[1]);
        return 1;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-two-clients-reg-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    child = fork();
    if (child < 0) {
        close(io_a[0]);
        close(io_a[1]);
        close(io_b[0]);
        close(io_b[1]);
        return 1;
    }

    if (child == 0) {
        execl(argv[1], argv[1], socket_path, (char *)NULL);
        _exit(127);
    }

    control_a = wait_for_connect(socket_path, child);
    if (control_a < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(io_a[0]);
        close(io_a[1]);
        close(io_b[0]);
        close(io_b[1]);
        return 1;
    }

    control_b = wait_for_connect(socket_path, child);
    if (control_b < 0) {
        close(control_a);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(io_a[0]);
        close(io_a[1]);
        close(io_b[0]);
        close(io_b[1]);
        return 1;
    }

    memset(&handshake_a, 0, sizeof(handshake_a));
    memset(&handshake_b, 0, sizeof(handshake_b));
    if (iouringd_client_handshake_fd(control_a, &handshake_a) != 0 ||
        iouringd_client_handshake_fd(control_b, &handshake_b) != 0 ||
        handshake_a.capabilities.registered_fd_slots == 0u ||
        handshake_b.capabilities.registered_fd_slots == 0u) {
        close(control_a);
        close(control_b);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(io_a[0]);
        close(io_a[1]);
        close(io_b[0]);
        close(io_b[1]);
        return 1;
    }

    memset(&resource_a, 0, sizeof(resource_a));
    memset(&resource_b, 0, sizeof(resource_b));
    if (iouringd_register_fd(control_a, io_a[0], &resource_a) != 0 ||
        iouringd_register_fd(control_b, io_b[0], &resource_b) != 0 ||
        resource_a.resource_id == IOURINGD_RESOURCE_ID_INVALID ||
        resource_b.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_a);
        close(control_b);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(io_a[0]);
        close(io_a[1]);
        close(io_b[0]);
        close(io_b[1]);
        return 1;
    }
    close(io_a[0]);
    io_a[0] = -1;
    close(io_b[0]);
    io_b[0] = -1;

    memset(&task_a, 0, sizeof(task_a));
    memset(&task_b, 0, sizeof(task_b));
    if (iouringd_submit_sock_write(control_a,
                                   resource_a.resource_id,
                                   message_a,
                                   (uint32_t)(sizeof(message_a) - 1u),
                                   &task_a) != 0 ||
        iouringd_submit_sock_write(control_b,
                                   resource_b.resource_id,
                                   message_b,
                                   (uint32_t)(sizeof(message_b) - 1u),
                                   &task_b) != 0) {
        close(control_a);
        close(control_b);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(io_a[1]);
        close(io_b[1]);
        return 1;
    }

    memset(peer_a, 0, sizeof(peer_a));
    memset(peer_b, 0, sizeof(peer_b));
    if (read_full(io_a[1], peer_a, sizeof(message_a) - 1u) != 0 ||
        read_full(io_b[1], peer_b, sizeof(message_b) - 1u) != 0 ||
        memcmp(peer_a, message_a, sizeof(message_a) - 1u) != 0 ||
        memcmp(peer_b, message_b, sizeof(message_b) - 1u) != 0) {
        close(control_a);
        close(control_b);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(io_a[1]);
        close(io_b[1]);
        return 1;
    }

    memset(&completion_a, 0, sizeof(completion_a));
    memset(&completion_b, 0, sizeof(completion_b));
    if (iouringd_wait_completion(control_a, &completion_a) != 0 ||
        iouringd_wait_completion(control_b, &completion_b) != 0 ||
        completion_a.task.task_id != task_a.task_id ||
        completion_b.task.task_id != task_b.task_id ||
        completion_a.task_kind.value != IOURINGD_TASK_KIND_SOCK_WRITE ||
        completion_b.task_kind.value != IOURINGD_TASK_KIND_SOCK_WRITE ||
        completion_a.res != (int32_t)(sizeof(message_a) - 1u) ||
        completion_b.res != (int32_t)(sizeof(message_b) - 1u)) {
        close(control_a);
        close(control_b);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(io_a[1]);
        close(io_b[1]);
        return 1;
    }

    close(control_a);
    close(control_b);
    close(io_a[1]);
    close(io_b[1]);

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
