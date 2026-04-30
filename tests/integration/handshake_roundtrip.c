#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "iouringd/client.h"

static int wait_for_handshake(const char *socket_path,
                              struct iouringd_handshake_result_v1 *result,
                              pid_t child)
{
    struct timespec delay;
    int attempt;

    delay.tv_sec = 0;
    delay.tv_nsec = 10000000L;

    for (attempt = 0; attempt < 100; ++attempt) {
        int status;
        pid_t rc = waitpid(child, &status, WNOHANG);

        if (rc == child) {
            return -1;
        }

        if (iouringd_client_handshake(socket_path, result) == 0) {
            return 0;
        }

        nanosleep(&delay, NULL);
    }

    return -1;
}

int main(int argc, char **argv)
{
    struct iouringd_handshake_result_v1 result;
    char socket_path[108];
    pid_t child;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(socket_path, sizeof(socket_path), "/tmp/iouringd-%ld.sock", (long)getpid());
    unlink(socket_path);

    child = fork();
    if (child < 0) {
        return 1;
    }

    if (child == 0) {
        execl(argv[1], argv[1], socket_path, (char *)NULL);
        _exit(127);
    }

    memset(&result, 0, sizeof(result));
    if (wait_for_handshake(socket_path, &result, child) != 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    if (result.response.header.version_major != IOURINGD_PROTOCOL_V1_MAJOR ||
        result.response.header.version_minor != IOURINGD_PROTOCOL_V1_MINOR ||
        result.response.abi_major != IOURINGD_ABI_V1_MAJOR ||
        result.response.abi_minor != IOURINGD_ABI_V1_MINOR ||
        result.capabilities.struct_size != sizeof(result.capabilities) ||
        result.capabilities.ring_entries == 0u ||
        result.capabilities.cq_entries == 0u ||
        result.capabilities.submit_credits == 0u ||
        result.capabilities.submit_credits > result.capabilities.ring_entries ||
        result.capabilities.registered_fd_slots == 0u ||
        (((result.capabilities.op_mask &
           (IOURINGD_CAPABILITY_OP_READ_FIXED |
            IOURINGD_CAPABILITY_OP_WRITE_FIXED)) != 0u) &&
         result.capabilities.registered_buffer_slots == 0u) ||
        result.capabilities.io_bytes_max == 0u ||
        (result.capabilities.op_mask & IOURINGD_CAPABILITY_OP_NOP) == 0u) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

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
