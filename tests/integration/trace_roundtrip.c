#define _POSIX_C_SOURCE 200809L

#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
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

int main(int argc, char **argv)
{
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_id_record_v1 resource;
    struct iouringd_task_id_record_v1 task;
    struct iouringd_trace_result_v1 trace;
    struct iouringd_trace_event_v1 events[8];
    size_t event_count = 0u;
    uint64_t cursor_after_submit = 0u;
    char socket_path[108];
    pid_t child;
    int control_fd = -1;
    int pair[2];
    int status;

    if (argc != 2) {
        return 2;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        return 1;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-trace-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    child = fork();
    if (child < 0) {
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    if (child == 0) {
        execl(argv[1], argv[1], socket_path, (char *)NULL);
        _exit(127);
    }

    control_fd = wait_for_connect(socket_path, child);
    if (control_fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_POLL) == 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    memset(&trace, 0, sizeof(trace));
    memset(events, 0, sizeof(events));
    event_count = 0u;
    if (iouringd_get_trace(control_fd,
                           0u,
                           8u,
                           &trace,
                           events,
                           sizeof(events) / sizeof(events[0]),
                           &event_count) != 0 ||
        trace.oldest_sequence != 0u ||
        trace.latest_sequence != 0u ||
        event_count != 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    memset(&resource, 0, sizeof(resource));
    if (iouringd_register_fd(control_fd, pair[0], &resource) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_poll(control_fd, resource.resource_id, POLLIN, &task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    if (write(pair[1], "x", 1u) != 1) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_POLL ||
        completion.res < 0 ||
        (completion.res & POLLIN) == 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    if (iouringd_release_resource(control_fd,
                                  resource.resource_id,
                                  &resource) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    memset(&trace, 0, sizeof(trace));
    memset(events, 0, sizeof(events));
    event_count = 0u;
    if (iouringd_get_trace(control_fd,
                           0u,
                           8u,
                           &trace,
                           events,
                           sizeof(events) / sizeof(events[0]),
                           &event_count) != 0 ||
        event_count != 4u ||
        trace.oldest_sequence != 1u ||
        trace.latest_sequence != 4u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    if (events[0].event_kind != IOURINGD_TRACE_EVENT_RESOURCE_REGISTER ||
        events[0].resource.resource_id != resource.resource_id ||
        events[0].priority != IOURINGD_SUBMIT_PRIORITY_NORMAL ||
        events[1].event_kind != IOURINGD_TRACE_EVENT_SUBMIT_ACCEPT ||
        events[1].task.task_id != task.task_id ||
        events[1].task_kind.value != IOURINGD_TASK_KIND_POLL ||
        events[1].priority != IOURINGD_SUBMIT_PRIORITY_NORMAL ||
        events[2].event_kind != IOURINGD_TRACE_EVENT_COMPLETION ||
        events[2].task.task_id != task.task_id ||
        events[2].task_kind.value != IOURINGD_TASK_KIND_POLL ||
        events[2].priority != IOURINGD_SUBMIT_PRIORITY_NORMAL ||
        (events[2].res & POLLIN) == 0 ||
        events[3].event_kind != IOURINGD_TRACE_EVENT_RESOURCE_RELEASE ||
        events[3].resource.resource_id != resource.resource_id ||
        events[3].priority != IOURINGD_SUBMIT_PRIORITY_NORMAL) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }
    cursor_after_submit = events[1].sequence;

    memset(&trace, 0, sizeof(trace));
    memset(events, 0, sizeof(events));
    event_count = 0u;
    if (iouringd_get_trace(control_fd,
                           cursor_after_submit,
                           8u,
                           &trace,
                           events,
                           sizeof(events) / sizeof(events[0]),
                           &event_count) != 0 ||
        event_count != 2u ||
        events[0].event_kind != IOURINGD_TRACE_EVENT_COMPLETION ||
        events[1].event_kind != IOURINGD_TRACE_EVENT_RESOURCE_RELEASE) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(pair[0]);
        close(pair[1]);
        return 1;
    }

    close(control_fd);
    close(pair[0]);
    close(pair[1]);

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
