#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
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

static int set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);

    if (flags < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
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

int main(int argc, char **argv)
{
    static const char message[] = "connected";
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_id_record_v1 resource;
    struct iouringd_task_id_record_v1 task;
    struct sockaddr_un listen_addr;
    struct sockaddr_un connect_addr;
    char daemon_socket_path[108];
    char listen_socket_path[108];
    char received[sizeof(message)];
    socklen_t connect_length;
    pid_t child;
    int accepted_fd = -1;
    int control_fd = -1;
    int io_fd = -1;
    int listen_fd = -1;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(daemon_socket_path,
             sizeof(daemon_socket_path),
             "/tmp/iouringd-connect-%ld.sock",
             (long)getpid());
    snprintf(listen_socket_path,
             sizeof(listen_socket_path),
             "/tmp/iouringd-connect-listener-%ld.sock",
             (long)getpid());
    unlink(daemon_socket_path);
    unlink(listen_socket_path);

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return 1;
    }
    if (set_cloexec(listen_fd) != 0) {
        close(listen_fd);
        return 1;
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sun_family = AF_UNIX;
    memcpy(listen_addr.sun_path,
           listen_socket_path,
           strlen(listen_socket_path) + 1u);
    if (bind(listen_fd, (const struct sockaddr *)&listen_addr, sizeof(listen_addr)) != 0 ||
        listen(listen_fd, 1) != 0) {
        close(listen_fd);
        unlink(listen_socket_path);
        return 1;
    }

    io_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (io_fd < 0 || set_cloexec(io_fd) != 0) {
        close(listen_fd);
        if (io_fd >= 0) {
            close(io_fd);
        }
        unlink(listen_socket_path);
        return 1;
    }

    child = fork();
    if (child < 0) {
        close(io_fd);
        close(listen_fd);
        unlink(listen_socket_path);
        return 1;
    }

    if (child == 0) {
        execl(argv[1], argv[1], daemon_socket_path, (char *)NULL);
        _exit(127);
    }

    control_fd = wait_for_connect(daemon_socket_path, child);
    if (control_fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(io_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_CONNECT) == 0u ||
        handshake.capabilities.registered_fd_slots == 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(io_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&resource, 0, sizeof(resource));
    if (iouringd_register_fd(control_fd, io_fd, &resource) != 0 ||
        resource.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(io_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&connect_addr, 0, sizeof(connect_addr));
    connect_addr.sun_family = AF_UNIX;
    memcpy(connect_addr.sun_path,
           listen_socket_path,
           strlen(listen_socket_path) + 1u);
    connect_length =
        (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                    strlen(listen_socket_path) + 1u);

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_connect(control_fd,
                                resource.resource_id,
                                &connect_addr,
                                (uint32_t)connect_length,
                                &task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(io_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_CONNECT ||
        completion.res != 0 ||
        completion.payload_length != 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(io_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    accepted_fd = accept(listen_fd, NULL, NULL);
    if (accepted_fd < 0 || set_cloexec(accepted_fd) != 0) {
        if (accepted_fd >= 0) {
            close(accepted_fd);
        }
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(io_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    if (write_full(io_fd, message, sizeof(message) - 1u) != 0 ||
        read_full(accepted_fd, received, sizeof(message) - 1u) != 0 ||
        memcmp(received, message, sizeof(message) - 1u) != 0) {
        close(accepted_fd);
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(io_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    close(accepted_fd);
    close(control_fd);
    close(io_fd);
    close(listen_fd);

    kill(child, SIGTERM);
    if (waitpid(child, &status, 0) != child) {
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    unlink(daemon_socket_path);
    unlink(listen_socket_path);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        return 1;
    }

    return 0;
}
