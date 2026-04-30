#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
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

static void ignore_signal(int signo)
{
    (void)signo;
}

static int arm_alarm_timer(long usec)
{
    struct itimerval timer;

    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_sec = usec / 1000000L;
    timer.it_value.tv_usec = usec % 1000000L;
    return setitimer(ITIMER_REAL, &timer, NULL);
}

static int exercise_response(struct iouringd_handshake_result_v1 *result)
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
            write_full(fds[1], result, sizeof(*result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    rc = iouringd_client_handshake_fd(fds[0], result);
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

static int exercise_interrupted_response(struct iouringd_handshake_result_v1 *result)
{
    struct iouringd_handshake_request_v1 request;
    struct sigaction action;
    struct sigaction previous;
    struct timespec delay;
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
        delay.tv_sec = 0;
        delay.tv_nsec = 50000000L;

        if (read_full(fds[1], &request, sizeof(request)) != 0 ||
            nanosleep(&delay, NULL) != 0 ||
            write_full(fds[1], result, sizeof(*result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    memset(&action, 0, sizeof(action));
    action.sa_handler = ignore_signal;
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGALRM, &action, &previous) != 0) {
        close(fds[0]);
        waitpid(child, &status, 0);
        return -1;
    }

    if (arm_alarm_timer(10000L) != 0) {
        close(fds[0]);
        sigaction(SIGALRM, &previous, NULL);
        waitpid(child, &status, 0);
        return -1;
    }
    errno = 0;
    rc = iouringd_client_handshake_fd(fds[0], result);
    if (arm_alarm_timer(0L) != 0) {
        close(fds[0]);
        sigaction(SIGALRM, &previous, NULL);
        waitpid(child, &status, 0);
        return -1;
    }
    close(fds[0]);
    sigaction(SIGALRM, &previous, NULL);

    if (waitpid(child, &status, 0) != child) {
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }

    return rc;
}

static int exercise_broken_peer(void)
{
    struct iouringd_handshake_result_v1 result;
    int fds[2];
    pid_t child;
    int status;

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
        close(fds[1]);
        memset(&result, 0, sizeof(result));
        errno = 0;
        if (iouringd_client_handshake_fd(fds[0], &result) != -1 || errno != EPIPE) {
            close(fds[0]);
            _exit(1);
        }

        close(fds[0]);
        _exit(0);
    }

    close(fds[0]);
    close(fds[1]);
    if (waitpid(child, &status, 0) != child) {
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
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
    result->capabilities.struct_size = sizeof(result->capabilities);
    result->capabilities.ring_entries = 2u;
    result->capabilities.cq_entries = 2u;
    result->capabilities.op_mask = IOURINGD_CAPABILITY_OP_NOP |
                                   IOURINGD_CAPABILITY_OP_TIMEOUT;
    result->capabilities.submit_credits = 1u;
    result->capabilities.registered_fd_slots = 1u;
    result->capabilities.registered_buffer_slots = 1u;
    result->capabilities.io_bytes_max = 1u;
}

int main(void)
{
    struct iouringd_handshake_result_v1 result;

    init_valid_result(&result);
    if (exercise_response(&result) != 0) {
        return 1;
    }

    init_valid_result(&result);
    if (exercise_interrupted_response(&result) != 0) {
        return 1;
    }

    if (exercise_broken_peer() != 0) {
        return 1;
    }

    init_valid_result(&result);
    result.response.header.magic =
        IOURINGD_PROTOCOL_WIRE_MAGIC ^ UINT32_C(0xFF);
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.response.header.version_major =
        (uint16_t)(IOURINGD_PROTOCOL_V1_MAJOR + 1u);
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.response.abi_major = (uint16_t)(IOURINGD_ABI_V1_MAJOR + 1u);
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.response.header.version_minor =
        (uint16_t)(IOURINGD_PROTOCOL_V1_MINOR + 1u);
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.response.abi_minor = (uint16_t)(IOURINGD_ABI_V1_MINOR + 1u);
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.response.reserved = 1u;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.response.status = IOURINGD_HANDSHAKE_STATUS_REJECT;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.response.status = IOURINGD_HANDSHAKE_STATUS_DEGRADE;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.struct_size =
        (uint32_t)(sizeof(result.capabilities) + 1u);
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.submit_credits = 0u;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.ring_entries = 0u;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.cq_entries = 0u;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.op_mask = 0u;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.op_mask = IOURINGD_CAPABILITY_OP_NOP |
                                  IOURINGD_CAPABILITY_OP_READV;
    if (exercise_response(&result) != 0) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.op_mask = UINT32_C(1) << 12;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.op_mask = IOURINGD_CAPABILITY_OP_NOP |
                                  IOURINGD_CAPABILITY_OP_READ_FIXED;
    if (exercise_response(&result) != 0) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.registered_fd_slots = 0u;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.op_mask = IOURINGD_CAPABILITY_OP_NOP |
                                  IOURINGD_CAPABILITY_OP_READ_FIXED;
    result.capabilities.registered_buffer_slots = 0u;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_result(&result);
    result.capabilities.io_bytes_max = 0u;
    if (exercise_response(&result) != -1 || errno != EPROTO) {
        return 1;
    }

    return 0;
}
