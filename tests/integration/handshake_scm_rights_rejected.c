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

#include "iouringd/api.h"
#include "iouringd/client.h"

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

static int write_full(int fd, const void *buf, size_t len)
{
    size_t offset = 0;
    const unsigned char *bytes = buf;

    while (offset < len) {
        ssize_t rc = send(fd, bytes + offset, len - offset, MSG_NOSIGNAL);

        if (rc <= 0) {
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

static int send_handshake_with_fd(int fd, int passed_fd)
{
    struct iouringd_handshake_request_v1 request;
    char control[CMSG_SPACE(sizeof(int))];
    struct iovec iov;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    ssize_t rc;

    memset(&request, 0, sizeof(request));
    request.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    request.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    request.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    request.abi_major = IOURINGD_ABI_V1_MAJOR;
    request.abi_minor = IOURINGD_ABI_V1_MINOR;

    memset(control, 0, sizeof(control));
    memset(&msg, 0, sizeof(msg));

    iov.iov_base = &request;
    iov.iov_len = sizeof(request);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &passed_fd, sizeof(passed_fd));

    for (;;) {
        rc = sendmsg(fd, &msg, MSG_NOSIGNAL);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        break;
    }

    if (rc != (ssize_t)sizeof(request)) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_register_fd_request_v1 register_request;
    struct iouringd_resource_result_v1 register_result;
    char daemon_socket_path[108];
    pid_t child;
    int control_fd = -1;
    int pipe_fds[2];
    int status;

    if (argc != 2) {
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);

    snprintf(daemon_socket_path,
             sizeof(daemon_socket_path),
             "/tmp/iouringd-scmrights-%ld.sock",
             (long)getpid());
    unlink(daemon_socket_path);

    if (pipe(pipe_fds) != 0) {
        return 1;
    }

    child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
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
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        unlink(daemon_socket_path);
        return 1;
    }

    /* Send a valid handshake but smuggle an fd over SCM_RIGHTS. */
    if (send_handshake_with_fd(control_fd, pipe_fds[0]) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        unlink(daemon_socket_path);
        return 1;
    }

    /* The daemon must not accept a handshake carrying ancillary data. */
    memset(&handshake, 0, sizeof(handshake));
    if (read_full(control_fd, &handshake, sizeof(handshake)) != 0 ||
        handshake.response.status == IOURINGD_HANDSHAKE_STATUS_ACCEPT) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        unlink(daemon_socket_path);
        return 1;
    }

    /*
     * Attempt to register a resource with no cmsg. The leftover handshake fd
     * must not survive into the SUBMIT phase: the rejected client is closed
     * deterministically, so no successful registration can come back.
     */
    memset(&register_request, 0, sizeof(register_request));
    register_request.control.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    register_request.control.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    register_request.control.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    register_request.control.request_kind = IOURINGD_REQUEST_KIND_REGISTER_FD;
    (void)write_full(control_fd, &register_request, sizeof(register_request));

    memset(&register_result, 0, sizeof(register_result));
    if (read_full(control_fd, &register_result, sizeof(register_result)) == 0) {
        /* A reply is only acceptable if it is not a successful registration. */
        if (register_result.resource.resource_id != IOURINGD_RESOURCE_ID_INVALID) {
            close(control_fd);
            kill(child, SIGTERM);
            waitpid(child, &status, 0);
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            unlink(daemon_socket_path);
            return 1;
        }
    }

    close(control_fd);
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    kill(child, SIGTERM);
    if (waitpid(child, &status, 0) != child) {
        unlink(daemon_socket_path);
        return 1;
    }

    unlink(daemon_socket_path);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        return 1;
    }

    return 0;
}
