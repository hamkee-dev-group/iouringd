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

static void init_valid_result(struct iouringd_handshake_result_v1 *result)
{
    memset(result, 0, sizeof(*result));
    result->response.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    result->response.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    result->response.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    result->response.abi_major = IOURINGD_ABI_V1_MAJOR;
    result->response.abi_minor = IOURINGD_ABI_V1_MINOR;
    result->response.status = IOURINGD_HANDSHAKE_STATUS_ACCEPT;
    result->capabilities.struct_size = (uint32_t)sizeof(result->capabilities);
    result->capabilities.ring_entries = 2u;
    result->capabilities.cq_entries = 2u;
    result->capabilities.op_mask = IOURINGD_CAPABILITY_OP_NOP;
    result->capabilities.submit_credits = 1u;
    result->capabilities.registered_fd_slots = 1u;
    result->capabilities.registered_buffer_slots = 1u;
    result->capabilities.io_bytes_max = 1u;
}

static int exercise_response(const struct iouringd_handshake_result_v1 *peer_result,
                             struct iouringd_handshake_result_v1 *client_result)
{
    struct iouringd_handshake_request_v1 request;
    int fds[2];
    pid_t child;
    int status;
    int rc;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return -1;
    }

    child = fork();
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (child == 0) {
        close(fds[0]);
        memset(&request, 0, sizeof(request));

        if (read_full(fds[1], &request, sizeof(request)) != 0 ||
            write_full(fds[1], peer_result, sizeof(*peer_result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    rc = iouringd_client_handshake_fd(fds[0], client_result);
    close(fds[0]);

    if (waitpid(child, &status, 0) != child) {
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }

    return rc;
}

int main(void)
{
    struct iouringd_handshake_result_v1 peer_result;
    struct iouringd_handshake_result_v1 client_result;

    init_valid_result(&peer_result);
    memset(&client_result, 0, sizeof(client_result));
    peer_result.response.abi_minor = (uint16_t)(IOURINGD_ABI_V1_MINOR + 1u);
    if (exercise_response(&peer_result, &client_result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&peer_result);
    memset(&client_result, 0, sizeof(client_result));
    peer_result.response.reserved = 1u;
    if (exercise_response(&peer_result, &client_result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&peer_result);
    memset(&client_result, 0, sizeof(client_result));
    peer_result.response.status = IOURINGD_HANDSHAKE_STATUS_REJECT;
    if (exercise_response(&peer_result, &client_result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&peer_result);
    memset(&client_result, 0, sizeof(client_result));
    peer_result.capabilities.struct_size =
        (uint32_t)(sizeof(struct iouringd_capability_descriptor_v1) + 1u);
    if (exercise_response(&peer_result, &client_result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&peer_result);
    memset(&client_result, 0, sizeof(client_result));
    peer_result.capabilities.submit_credits = 0u;
    if (exercise_response(&peer_result, &client_result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&peer_result);
    memset(&client_result, 0, sizeof(client_result));
    peer_result.capabilities.op_mask = IOURINGD_CAPABILITY_OP_READ_FIXED;
    peer_result.capabilities.registered_buffer_slots = 0u;
    if (exercise_response(&peer_result, &client_result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&peer_result);
    memset(&client_result, 0, sizeof(client_result));
    if (exercise_response(&peer_result, &client_result) != 0) {
        return 1;
    }

    if (client_result.response.abi_minor != IOURINGD_ABI_V1_MINOR ||
        client_result.response.reserved != 0u ||
        client_result.capabilities.struct_size != sizeof(client_result.capabilities) ||
        client_result.capabilities.op_mask != IOURINGD_CAPABILITY_OP_NOP ||
        client_result.capabilities.submit_credits != 1u ||
        client_result.capabilities.registered_fd_slots != 1u ||
        client_result.capabilities.registered_buffer_slots != 1u ||
        client_result.capabilities.io_bytes_max != 1u) {
        return 1;
    }

    return 0;
}
