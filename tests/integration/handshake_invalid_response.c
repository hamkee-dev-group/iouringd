#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "iouringd/client.h"

static int write_full(int fd, const void *buf, size_t len)
{
    size_t offset = 0;
    const char *bytes = buf;

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
    char *bytes = buf;

    while (offset < len) {
        ssize_t rc = read(fd, bytes + offset, len - offset);

        if (rc <= 0) {
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

int main(void)
{
    struct iouringd_handshake_request_v1 request;
    struct iouringd_handshake_result_v1 result;
    int fds[2];
    pid_t child;
    int status;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return 1;
    }

    child = fork();
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        return 1;
    }

    if (child == 0) {
        close(fds[0]);
        memset(&request, 0, sizeof(request));
        memset(&result, 0, sizeof(result));
        result.response.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
        result.response.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
        result.response.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
        result.response.abi_major = IOURINGD_ABI_V1_MAJOR;
        result.response.abi_minor = (uint16_t)(IOURINGD_ABI_V1_MINOR + 1u);
        result.response.status = IOURINGD_HANDSHAKE_STATUS_ACCEPT;

        if (read_full(fds[1], &request, sizeof(request)) != 0 ||
            write_full(fds[1], &result, sizeof(result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    memset(&result, 0, sizeof(result));
    errno = 0;
    if (iouringd_client_handshake_fd(fds[0], &result) != -1 || errno != EPROTO) {
        close(fds[0]);
        waitpid(child, &status, 0);
        return 1;
    }

    close(fds[0]);
    if (waitpid(child, &status, 0) != child) {
        return 1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return 1;
    }

    return 0;
}
