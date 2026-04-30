#define _POSIX_C_SOURCE 200809L

#include <errno.h>
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
#include "iouringd/submit.h"

static int write_full(int fd, const void *buf, size_t len)
{
    size_t offset = 0;
    const char *bytes = buf;

    while (offset < len) {
        ssize_t rc = write(fd, bytes + offset, len - offset);

        if (rc < 0 && errno == EINTR) {
            continue;
        }

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

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        if (rc <= 0) {
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

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

static int wait_for_stop(pid_t child)
{
    struct timespec delay;
    int attempt;

    delay.tv_sec = 0;
    delay.tv_nsec = 10000000L;

    for (attempt = 0; attempt < 100; ++attempt) {
        int status;
        pid_t rc = waitpid(child, &status, WUNTRACED | WNOHANG);

        if (rc == child && WIFSTOPPED(status)) {
            return 0;
        }

        nanosleep(&delay, NULL);
    }

    return -1;
}

int main(int argc, char **argv)
{
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 first_handshake;
    struct iouringd_handshake_result_v1 second_handshake;
    struct iouringd_submit_result_v1 high_result;
    struct iouringd_submit_result_v1 low_result;
    struct iouringd_trace_result_v1 trace;
    struct iouringd_trace_event_v1 events[8];
    size_t event_count = 0u;
    char socket_path[108];
    pid_t daemon_child;
    int first_fd;
    int second_fd;
    int status;

    if (argc != 2) {
        return 2;
    }

    alarm(10);
    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-priority-dispatch-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    daemon_child = fork();
    if (daemon_child < 0) {
        return 1;
    }

    if (daemon_child == 0) {
        execl(argv[1],
              argv[1],
              "--ring-entries",
              "2",
              "--max-clients",
              "2",
              "--per-client-credits",
              "1",
              socket_path,
              (char *)NULL);
        _exit(127);
    }

    first_fd = wait_for_connect(socket_path, daemon_child);
    if (first_fd < 0) {
        kill(daemon_child, SIGTERM);
        waitpid(daemon_child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    second_fd = wait_for_connect(socket_path, daemon_child);
    if (second_fd < 0) {
        close(first_fd);
        kill(daemon_child, SIGTERM);
        waitpid(daemon_child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&first_handshake, 0, sizeof(first_handshake));
    memset(&second_handshake, 0, sizeof(second_handshake));
    if (iouringd_client_handshake_fd(first_fd, &first_handshake) != 0 ||
        iouringd_client_handshake_fd(second_fd, &second_handshake) != 0 ||
        (first_handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_TIMEOUT) == 0u) {
        close(first_fd);
        close(second_fd);
        kill(daemon_child, SIGTERM);
        waitpid(daemon_child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    {
        struct timespec settle_delay;

        settle_delay.tv_sec = 0;
        settle_delay.tv_nsec = 50000000L;
        nanosleep(&settle_delay, NULL);
    }

    {
        struct iouringd_timeout_request_v1 low_request;
        struct iouringd_timeout_request_v1 high_request;

        if (kill(daemon_child, SIGSTOP) != 0 || wait_for_stop(daemon_child) != 0) {
            close(first_fd);
            close(second_fd);
            kill(daemon_child, SIGTERM);
            waitpid(daemon_child, &status, 0);
            unlink(socket_path);
            return 1;
        }

        memset(&low_request, 0, sizeof(low_request));
        low_request.submit.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
        low_request.submit.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
        low_request.submit.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
        low_request.submit.task_kind.value = IOURINGD_TASK_KIND_TIMEOUT;
        low_request.submit.priority = IOURINGD_SUBMIT_PRIORITY_LOW;
        low_request.timeout_nsec = UINT64_C(50000000);

        memset(&high_request, 0, sizeof(high_request));
        high_request.submit.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
        high_request.submit.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
        high_request.submit.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
        high_request.submit.task_kind.value = IOURINGD_TASK_KIND_TIMEOUT;
        high_request.submit.priority = IOURINGD_SUBMIT_PRIORITY_HIGH;
        high_request.timeout_nsec = UINT64_C(50000000);

        if (write_full(first_fd, &low_request, sizeof(low_request)) != 0 ||
            write_full(second_fd, &high_request, sizeof(high_request)) != 0 ||
            kill(daemon_child, SIGCONT) != 0) {
            close(first_fd);
            close(second_fd);
            kill(daemon_child, SIGTERM);
            waitpid(daemon_child, &status, 0);
            unlink(socket_path);
            return 1;
        }
    }

    memset(&high_result, 0, sizeof(high_result));
    memset(&low_result, 0, sizeof(low_result));
    if (read_full(second_fd, &high_result, sizeof(high_result)) != 0 ||
        read_full(first_fd, &low_result, sizeof(low_result)) != 0) {
        close(first_fd);
        close(second_fd);
        kill(daemon_child, SIGTERM);
        waitpid(daemon_child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    if (high_result.task.task_id == IOURINGD_TASK_ID_INVALID ||
        high_result.res != IOURINGD_COMPLETION_RES_OK ||
        low_result.task.task_id != IOURINGD_TASK_ID_INVALID ||
        low_result.res != -EAGAIN) {
        close(first_fd);
        close(second_fd);
        kill(daemon_child, SIGTERM);
        waitpid(daemon_child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(second_fd, &completion) != 0 ||
        completion.task.task_id != high_result.task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_TIMEOUT ||
        completion.res != -ETIME) {
        close(first_fd);
        close(second_fd);
        kill(daemon_child, SIGTERM);
        waitpid(daemon_child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&trace, 0, sizeof(trace));
    memset(events, 0, sizeof(events));
    if (iouringd_get_trace(first_fd,
                           0u,
                           8u,
                           &trace,
                           events,
                           sizeof(events) / sizeof(events[0]),
                           &event_count) != 0 ||
        event_count != 3u ||
        events[0].event_kind != IOURINGD_TRACE_EVENT_SUBMIT_ACCEPT ||
        events[0].priority != IOURINGD_SUBMIT_PRIORITY_HIGH ||
        events[1].event_kind != IOURINGD_TRACE_EVENT_SUBMIT_REJECT ||
        events[1].priority != IOURINGD_SUBMIT_PRIORITY_LOW ||
        events[2].event_kind != IOURINGD_TRACE_EVENT_COMPLETION ||
        events[2].priority != IOURINGD_SUBMIT_PRIORITY_HIGH) {
        close(first_fd);
        close(second_fd);
        kill(daemon_child, SIGTERM);
        waitpid(daemon_child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    close(first_fd);
    close(second_fd);

    kill(daemon_child, SIGTERM);
    if (waitpid(daemon_child, &status, 0) != daemon_child) {
        unlink(socket_path);
        return 1;
    }

    unlink(socket_path);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        return 1;
    }

    return 0;
}
