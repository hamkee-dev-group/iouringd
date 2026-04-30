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

int main(int argc, char **argv)
{
    static const char message[] = "accepted";
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_id_record_v1 listener_resource;
    struct iouringd_task_id_record_v1 accept_task;
    struct iouringd_task_id_record_v1 write_task;
    struct sockaddr_un listen_addr;
    struct sockaddr_un peer_addr;
    struct sockaddr_un payload_addr;
    char daemon_socket_path[108];
    char listen_socket_path[108];
    char client_socket_path[108];
    char received[sizeof(message)];
    size_t payload_length = 0u;
    pid_t child;
    int control_fd = -1;
    int listen_fd = -1;
    int peer_fd = -1;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(daemon_socket_path,
             sizeof(daemon_socket_path),
             "/tmp/iouringd-accept-%ld.sock",
             (long)getpid());
    snprintf(listen_socket_path,
             sizeof(listen_socket_path),
             "/tmp/iouringd-accept-listener-%ld.sock",
             (long)getpid());
    snprintf(client_socket_path,
             sizeof(client_socket_path),
             "/tmp/iouringd-accept-client-%ld.sock",
             (long)getpid());
    unlink(daemon_socket_path);
    unlink(listen_socket_path);
    unlink(client_socket_path);

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0 || set_cloexec(listen_fd) != 0) {
        if (listen_fd >= 0) {
            close(listen_fd);
        }
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

    peer_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (peer_fd < 0 || set_cloexec(peer_fd) != 0) {
        close(listen_fd);
        if (peer_fd >= 0) {
            close(peer_fd);
        }
        unlink(listen_socket_path);
        return 1;
    }

    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sun_family = AF_UNIX;
    memcpy(peer_addr.sun_path,
           client_socket_path,
           strlen(client_socket_path) + 1u);
    if (bind(peer_fd, (const struct sockaddr *)&peer_addr, sizeof(peer_addr)) != 0) {
        close(peer_fd);
        close(listen_fd);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    child = fork();
    if (child < 0) {
        close(peer_fd);
        close(listen_fd);
        unlink(listen_socket_path);
        unlink(client_socket_path);
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
        close(peer_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_ACCEPT) == 0u ||
        handshake.capabilities.registered_fd_slots < 2u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(peer_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    memset(&listener_resource, 0, sizeof(listener_resource));
    if (iouringd_register_fd(control_fd, listen_fd, &listener_resource) != 0 ||
        listener_resource.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(peer_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    memset(&accept_task, 0, sizeof(accept_task));
    if (iouringd_submit_accept(control_fd,
                               listener_resource.resource_id,
                               (uint32_t)sizeof(payload_addr),
                               &accept_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(peer_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    if (connect(peer_fd, (const struct sockaddr *)&listen_addr, sizeof(listen_addr)) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(peer_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != accept_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_ACCEPT ||
        completion.res <= 0 ||
        completion.payload_length == 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(peer_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    memset(&payload_addr, 0, sizeof(payload_addr));
    if (iouringd_read_completion_payload(control_fd,
                                         &completion,
                                         &payload_addr,
                                         sizeof(payload_addr),
                                         &payload_length) != 0 ||
        payload_length != completion.payload_length ||
        payload_length <= offsetof(struct sockaddr_un, sun_path) ||
        payload_addr.sun_family != AF_UNIX ||
        strcmp(payload_addr.sun_path, client_socket_path) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(peer_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    memset(&write_task, 0, sizeof(write_task));
    if (iouringd_submit_sock_write(control_fd,
                                   (iouringd_resource_id_t)completion.res,
                                   message,
                                   (uint32_t)(sizeof(message) - 1u),
                                   &write_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(peer_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != write_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_SOCK_WRITE ||
        completion.res != (int32_t)(sizeof(message) - 1u) ||
        completion.payload_length != 0u ||
        read_full(peer_fd, received, sizeof(message) - 1u) != 0 ||
        memcmp(received, message, sizeof(message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(peer_fd);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    close(control_fd);
    close(peer_fd);
    close(listen_fd);

    kill(child, SIGTERM);
    if (waitpid(child, &status, 0) != child) {
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(client_socket_path);
        return 1;
    }

    unlink(daemon_socket_path);
    unlink(listen_socket_path);
    unlink(client_socket_path);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        return 1;
    }

    return 0;
}
