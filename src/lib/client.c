#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "iouringd/client.h"
#include "iouringd/version.h"

static int set_cloexec(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        return -1;
    }

    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) {
        return -1;
    }

    return 0;
}

static int write_full(int fd, const void *buf, size_t len)
{
    size_t offset = 0;
    const char *bytes = buf;

    while (offset < len) {
        ssize_t rc;

        rc = send(fd,
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

int iouringd_client_connect(const char *socket_path)
{
    struct sockaddr_un addr;
    int fd;

    if (socket_path == NULL) {
        errno = EINVAL;
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    if (set_cloexec(fd) != 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(addr.sun_path, socket_path, strlen(socket_path) + 1u);

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int iouringd_client_handshake_fd(int fd,
                                 struct iouringd_handshake_result_v1 *result)
{
    struct iouringd_handshake_request_v1 request;

    if (fd < 0 || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    request.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    request.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    request.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    request.abi_major = IOURINGD_ABI_V1_MAJOR;
    request.abi_minor = IOURINGD_ABI_V1_MINOR;

    if (write_full(fd, &request, sizeof(request)) != 0 ||
        read_full(fd, result, sizeof(*result)) != 0) {
        return -1;
    }

    if (result->response.header.magic != IOURINGD_PROTOCOL_WIRE_MAGIC ||
        result->response.header.version_major != IOURINGD_PROTOCOL_V1_MAJOR ||
        result->response.header.version_minor != IOURINGD_PROTOCOL_V1_MINOR ||
        result->response.abi_major != IOURINGD_ABI_V1_MAJOR ||
        result->response.abi_minor != IOURINGD_ABI_V1_MINOR ||
        result->response.reserved != 0u ||
        result->response.status != IOURINGD_HANDSHAKE_STATUS_ACCEPT ||
        result->capabilities.struct_size !=
            sizeof(struct iouringd_capability_descriptor_v1) ||
        result->capabilities.ring_entries == 0u ||
        result->capabilities.cq_entries == 0u ||
        result->capabilities.op_mask == 0u ||
        (result->capabilities.op_mask & ~IOURINGD_CAPABILITY_OP_MASK_V1) != 0u ||
        result->capabilities.submit_credits == 0u ||
        result->capabilities.submit_credits > result->capabilities.ring_entries ||
        result->capabilities.registered_fd_slots == 0u ||
        result->capabilities.io_bytes_max == 0u) {
        errno = EPROTO;
        return -1;
    }

    if ((result->capabilities.op_mask &
         (IOURINGD_CAPABILITY_OP_READ_FIXED |
          IOURINGD_CAPABILITY_OP_WRITE_FIXED)) != 0u &&
        result->capabilities.registered_buffer_slots == 0u) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int iouringd_client_handshake(const char *socket_path,
                              struct iouringd_handshake_result_v1 *result)
{
    int fd;
    int rc;

    if (result == NULL) {
        errno = EINVAL;
        return -1;
    }

    fd = iouringd_client_connect(socket_path);
    if (fd < 0) {
        return -1;
    }

    rc = iouringd_client_handshake_fd(fd, result);
    close(fd);
    return rc;
}

const char *iouringd_version_string(void)
{
    return IOURINGD_VERSION_STRING;
}

const char *iouringd_client_version(void)
{
    return IOURINGD_VERSION_STRING;
}
