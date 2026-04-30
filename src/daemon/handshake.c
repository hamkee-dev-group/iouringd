#include "handshake.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int read_full(int fd, void *buf, size_t len)
{
    size_t offset = 0;
    char *bytes = buf;

    while (offset < len) {
        ssize_t rc = recv(fd, bytes + offset, len - offset, 0);

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        if (rc < 0) {
            return -1;
        }

        if (rc == 0) {
            errno = EPIPE;
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

static int write_full(int fd, const void *buf, size_t len)
{
    size_t offset = 0;
    const char *bytes = buf;

    while (offset < len) {
        ssize_t rc = send(fd,
                          bytes + offset,
                          len - offset,
#ifdef MSG_NOSIGNAL
                          MSG_NOSIGNAL
#else
                          0
#endif
        );

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        if (rc < 0) {
            return -1;
        }

        if (rc == 0) {
            errno = EPIPE;
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

int iouringd_serve_handshake_client(
    int client_fd,
    const struct iouringd_capability_descriptor_v1 *capabilities,
    uint16_t *status)
{
    struct iouringd_handshake_request_v1 request;
    struct iouringd_handshake_result_v1 result;

    if (read_full(client_fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    memset(&result, 0, sizeof(result));
    result.response.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    result.response.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    result.response.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    result.response.abi_major = IOURINGD_ABI_V1_MAJOR;
    result.response.abi_minor = IOURINGD_ABI_V1_MINOR;
    result.response.status = IOURINGD_HANDSHAKE_STATUS_ACCEPT;

    if (request.header.magic != IOURINGD_PROTOCOL_WIRE_MAGIC ||
        request.header.version_major != IOURINGD_PROTOCOL_V1_MAJOR ||
        request.header.version_minor != IOURINGD_PROTOCOL_V1_MINOR ||
        request.abi_major != IOURINGD_ABI_V1_MAJOR ||
        request.abi_minor != IOURINGD_ABI_V1_MINOR ||
        request.reserved != 0u) {
        result.response.status = IOURINGD_HANDSHAKE_STATUS_REJECT;
    } else {
        result.capabilities = *capabilities;
    }

    if (write_full(client_fd, &result, sizeof(result)) != 0) {
        return -1;
    }

    if (status != NULL) {
        *status = result.response.status;
    }

    return 0;
}
