#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "iouringd/submit.h"

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

static int exercise_submit_result(int (*submit_fn)(int,
                                                   struct iouringd_submit_result_v1 *),
                                  const struct iouringd_submit_result_v1 *wire_result,
                                  struct iouringd_submit_result_v1 *client_result)
{
    struct iouringd_submit_request_v1 request;
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
            write_full(fds[1], wire_result, sizeof(*wire_result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    memset(client_result, 0, sizeof(*client_result));
    rc = submit_fn(fds[0], client_result);
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

static int exercise_interrupted_submit_result(
    const struct iouringd_submit_result_v1 *wire_result,
    struct iouringd_submit_result_v1 *client_result)
{
    struct iouringd_submit_request_v1 request;
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
            write_full(fds[1], wire_result, sizeof(*wire_result)) != 0) {
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
    memset(client_result, 0, sizeof(*client_result));
    rc = iouringd_submit_nop_result(fds[0], client_result);
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

static int exercise_broken_pipe_submit(void)
{
    struct iouringd_submit_result_v1 result;
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
        if (iouringd_submit_nop_result(fds[0], &result) != -1 || errno != EPIPE) {
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

static int exercise_broken_pipe_register_fd(void)
{
    struct iouringd_resource_result_v1 result;
    int fds[2];
    int pipe_fds[2];
    pid_t child;
    int status;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return -1;
    }

    if (pipe(pipe_fds) != 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    child = fork();
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }

    if (child == 0) {
        close(fds[1]);
        close(pipe_fds[0]);
        memset(&result, 0, sizeof(result));
        errno = 0;
        if (iouringd_register_fd_result(fds[0], pipe_fds[1], &result) != -1 ||
            errno != EPIPE) {
            close(pipe_fds[1]);
            close(fds[0]);
            _exit(1);
        }

        close(pipe_fds[1]);
        close(fds[0]);
        _exit(0);
    }

    close(fds[0]);
    close(fds[1]);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    if (waitpid(child, &status, 0) != child) {
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int exercise_broken_pipe_write_iov(void)
{
    static const unsigned char payload[] = {1u, 2u, 3u, 4u};
    struct iouringd_submit_result_v1 result;
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
        if (iouringd_submit_sock_write_result(fds[0],
                                              UINT32_C(7),
                                              payload,
                                              (uint32_t)sizeof(payload),
                                              &result) != -1 ||
            errno != EPIPE) {
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

static int exercise_submit_timeout_result(
    const struct iouringd_submit_result_v1 *wire_result,
    struct iouringd_submit_result_v1 *client_result)
{
    struct iouringd_timeout_request_v1 request;
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
            write_full(fds[1], wire_result, sizeof(*wire_result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    memset(client_result, 0, sizeof(*client_result));
    rc = iouringd_submit_timeout_result(fds[0], UINT64_C(1000000), client_result);
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

static int exercise_submit_cancel_result(
    const struct iouringd_submit_result_v1 *wire_result,
    struct iouringd_submit_result_v1 *client_result)
{
    struct iouringd_cancel_request_v1 request;
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
            write_full(fds[1], wire_result, sizeof(*wire_result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    memset(client_result, 0, sizeof(*client_result));
    rc = iouringd_submit_cancel_result(fds[0], UINT64_C(7), client_result);
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

static int exercise_submit_poll_result(
    const struct iouringd_submit_result_v1 *wire_result,
    struct iouringd_submit_result_v1 *client_result)
{
    struct iouringd_poll_request_v1 request;
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
            write_full(fds[1], wire_result, sizeof(*wire_result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    memset(client_result, 0, sizeof(*client_result));
    rc = iouringd_submit_poll_result(fds[0], UINT32_C(3), POLLIN, client_result);
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

static int exercise_submit_connect_result(
    const struct iouringd_submit_result_v1 *wire_result,
    struct iouringd_submit_result_v1 *client_result)
{
    static const unsigned char sockaddr_bytes[] = {1u, 2u, 3u, 4u};
    struct iouringd_connect_request_v1 request;
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
        unsigned char received[sizeof(sockaddr_bytes)];

        close(fds[0]);
        memset(&request, 0, sizeof(request));
        memset(received, 0, sizeof(received));

        if (read_full(fds[1], &request, sizeof(request)) != 0 ||
            read_full(fds[1], received, sizeof(received)) != 0 ||
            memcmp(received, sockaddr_bytes, sizeof(sockaddr_bytes)) != 0 ||
            write_full(fds[1], wire_result, sizeof(*wire_result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    memset(client_result, 0, sizeof(*client_result));
    rc = iouringd_submit_connect_result(fds[0],
                                        UINT32_C(9),
                                        sockaddr_bytes,
                                        (uint32_t)sizeof(sockaddr_bytes),
                                        client_result);
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

static int exercise_submit_accept_result(
    const struct iouringd_submit_result_v1 *wire_result,
    struct iouringd_submit_result_v1 *client_result)
{
    struct iouringd_accept_request_v1 request;
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
            write_full(fds[1], wire_result, sizeof(*wire_result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    memset(client_result, 0, sizeof(*client_result));
    rc = iouringd_submit_accept_result(fds[0], UINT32_C(11), UINT32_C(32), client_result);
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

static int exercise_submit_openat_result(
    const struct iouringd_submit_result_v1 *wire_result,
    struct iouringd_submit_result_v1 *client_result)
{
    static const char path[] = "/tmp/iouringd-openat-validation";
    struct iouringd_openat_request_v1 request;
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
        char received[sizeof(path)];

        close(fds[0]);
        memset(&request, 0, sizeof(request));
        memset(received, 0, sizeof(received));

        if (read_full(fds[1], &request, sizeof(request)) != 0 ||
            read_full(fds[1], received, sizeof(received)) != 0 ||
            memcmp(received, path, sizeof(path)) != 0 ||
            write_full(fds[1], wire_result, sizeof(*wire_result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    memset(client_result, 0, sizeof(*client_result));
    rc = iouringd_submit_openat_result(fds[0],
                                       path,
                                       O_RDONLY,
                                       0u,
                                       client_result);
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

static int exercise_submit_close_result(
    const struct iouringd_submit_result_v1 *wire_result,
    struct iouringd_submit_result_v1 *client_result)
{
    struct iouringd_close_request_v1 request;
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
            request.resource.resource_id != UINT32_C(13) ||
            write_full(fds[1], wire_result, sizeof(*wire_result)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    memset(client_result, 0, sizeof(*client_result));
    rc = iouringd_submit_close_result(fds[0], UINT32_C(13), client_result);
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

static void init_accept(struct iouringd_submit_result_v1 *result,
                        iouringd_task_id_t task_id,
                        uint32_t credits)
{
    memset(result, 0, sizeof(*result));
    result->task.task_id = task_id;
    result->res = IOURINGD_COMPLETION_RES_OK;
    result->credits = credits;
}

static void init_reject(struct iouringd_submit_result_v1 *result,
                        int32_t res,
                        uint32_t credits)
{
    memset(result, 0, sizeof(*result));
    result->task.task_id = IOURINGD_TASK_ID_INVALID;
    result->res = res;
    result->credits = credits;
}

int main(void)
{
    struct iouringd_submit_request_v1 request;
    struct iouringd_submit_result_v1 wire_result;
    struct iouringd_submit_result_v1 client_result;

    memset(&request, 0, sizeof(request));
    if (iouringd_submit_request_priority(&request) != IOURINGD_SUBMIT_PRIORITY_NORMAL) {
        return 1;
    }

    errno = 0;
    if (iouringd_submit_request_set_priority(NULL, IOURINGD_SUBMIT_PRIORITY_HIGH) != -1 ||
        errno != EINVAL) {
        return 1;
    }

    errno = 0;
    if (iouringd_submit_request_set_priority(&request,
                                             IOURINGD_SUBMIT_PRIORITY_MAX_V1) != -1 ||
        errno != EINVAL) {
        return 1;
    }

    if (iouringd_submit_request_set_priority(&request,
                                             IOURINGD_SUBMIT_PRIORITY_HIGH) != 0 ||
        iouringd_submit_request_priority(&request) != IOURINGD_SUBMIT_PRIORITY_HIGH ||
        iouringd_submit_priority_is_valid(IOURINGD_SUBMIT_PRIORITY_LOW) == 0 ||
        iouringd_submit_priority_is_valid(IOURINGD_SUBMIT_PRIORITY_MAX_V1) != 0) {
        return 1;
    }

    init_accept(&wire_result, UINT64_C(1), 2u);
    if (exercise_submit_result(iouringd_submit_nop_result,
                               &wire_result,
                               &client_result) != 0 ||
        client_result.task.task_id != UINT64_C(1) ||
        client_result.credits != 2u) {
        return 1;
    }

    init_accept(&wire_result, UINT64_C(9), 1u);
    if (exercise_interrupted_submit_result(&wire_result, &client_result) != 0 ||
        client_result.task.task_id != UINT64_C(9) ||
        client_result.credits != 1u) {
        return 1;
    }

    if (exercise_broken_pipe_submit() != 0 ||
        exercise_broken_pipe_register_fd() != 0 ||
        exercise_broken_pipe_write_iov() != 0) {
        return 1;
    }

    init_accept(&wire_result, UINT64_C(2), 1u);
    if (exercise_submit_timeout_result(&wire_result, &client_result) != 0 ||
        client_result.task.task_id != UINT64_C(2) ||
        client_result.credits != 1u) {
        return 1;
    }

    init_accept(&wire_result, UINT64_C(3), 0u);
    if (exercise_submit_cancel_result(&wire_result, &client_result) != 0 ||
        client_result.task.task_id != UINT64_C(3)) {
        return 1;
    }

    init_accept(&wire_result, UINT64_C(4), 3u);
    if (exercise_submit_poll_result(&wire_result, &client_result) != 0 ||
        client_result.task.task_id != UINT64_C(4) ||
        client_result.credits != 3u) {
        return 1;
    }

    init_accept(&wire_result, UINT64_C(5), 2u);
    if (exercise_submit_connect_result(&wire_result, &client_result) != 0 ||
        client_result.task.task_id != UINT64_C(5) ||
        client_result.credits != 2u) {
        return 1;
    }

    init_accept(&wire_result, UINT64_C(6), 1u);
    if (exercise_submit_accept_result(&wire_result, &client_result) != 0 ||
        client_result.task.task_id != UINT64_C(6) ||
        client_result.credits != 1u) {
        return 1;
    }

    init_accept(&wire_result, UINT64_C(7), 1u);
    if (exercise_submit_openat_result(&wire_result, &client_result) != 0 ||
        client_result.task.task_id != UINT64_C(7) ||
        client_result.credits != 1u) {
        return 1;
    }

    init_accept(&wire_result, UINT64_C(8), 1u);
    if (exercise_submit_close_result(&wire_result, &client_result) != 0 ||
        client_result.task.task_id != UINT64_C(8) ||
        client_result.credits != 1u) {
        return 1;
    }

    init_reject(&wire_result, -EAGAIN, 0u);
    if (exercise_submit_result(iouringd_submit_nop_result,
                               &wire_result,
                               &client_result) != -1 ||
        errno != EAGAIN ||
        client_result.credits != 0u) {
        return 1;
    }

    init_reject(&wire_result, -EINVAL, 2u);
    if (exercise_submit_cancel_result(&wire_result, &client_result) != -1 ||
        errno != EINVAL ||
        client_result.credits != 2u) {
        return 1;
    }

    init_accept(&wire_result, IOURINGD_TASK_ID_INVALID, 1u);
    if (exercise_submit_result(iouringd_submit_nop_result,
                               &wire_result,
                               &client_result) != -1 ||
        errno != EPROTO) {
        return 1;
    }

    init_accept(&wire_result, IOURINGD_TASK_ID_RESERVED_MIN, 1u);
    if (exercise_submit_timeout_result(&wire_result, &client_result) != -1 ||
        errno != EPROTO) {
        return 1;
    }

    memset(&wire_result, 0, sizeof(wire_result));
    wire_result.task.task_id = UINT64_C(9);
    wire_result.res = -EINVAL;
    wire_result.credits = 1u;
    if (exercise_submit_cancel_result(&wire_result, &client_result) != -1 ||
        errno != EPROTO) {
        return 1;
    }

    return 0;
}
