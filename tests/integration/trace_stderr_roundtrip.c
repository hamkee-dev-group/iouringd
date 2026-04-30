#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
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

static ssize_t read_all(int fd, char *buf, size_t capacity)
{
    size_t offset = 0u;

    while (offset + 1u < capacity) {
        ssize_t rc = read(fd, buf + offset, capacity - offset - 1u);

        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            break;
        }
        offset += (size_t)rc;
    }

    buf[offset] = '\0';
    return (ssize_t)offset;
}

int main(int argc, char **argv)
{
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_task_id_record_v1 task;
    char socket_path[108];
    char stderr_buf[4096];
    char task_id_field[64];
    int stderr_pipe[2];
    pid_t child;
    int fd = -1;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-trace-stderr-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    if (pipe(stderr_pipe) != 0) {
        return 1;
    }

    child = fork();
    if (child < 0) {
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return 1;
    }

    if (child == 0) {
        close(stderr_pipe[0]);
        if (dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        close(stderr_pipe[1]);
        execl(argv[1],
              argv[1],
              "--trace-stderr",
              "--job-id",
              "42",
              socket_path,
              (char *)NULL);
        _exit(127);
    }

    close(stderr_pipe[1]);
    fd = wait_for_connect(socket_path, child);
    if (fd < 0) {
        close(stderr_pipe[0]);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(fd, &handshake) != 0) {
        close(fd);
        close(stderr_pipe[0]);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_nop(fd, &task) != 0) {
        close(fd);
        close(stderr_pipe[0]);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(fd, &completion) != 0 ||
        completion.task.task_id != task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_NOP ||
        completion.res != IOURINGD_COMPLETION_RES_OK) {
        close(fd);
        close(stderr_pipe[0]);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    close(fd);
    kill(child, SIGTERM);
    if (waitpid(child, &status, 0) != child) {
        close(stderr_pipe[0]);
        unlink(socket_path);
        return 1;
    }

    memset(stderr_buf, 0, sizeof(stderr_buf));
    if (read_all(stderr_pipe[0], stderr_buf, sizeof(stderr_buf)) < 0) {
        close(stderr_pipe[0]);
        unlink(socket_path);
        return 1;
    }

    close(stderr_pipe[0]);
    unlink(socket_path);

    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        return 1;
    }

    snprintf(task_id_field, sizeof(task_id_field), "task_id=%" PRIu64, task.task_id);

    if (strstr(stderr_buf, "iouringd metrics phase=ready") == NULL ||
        strstr(stderr_buf, "iouringd metrics phase=shutdown") == NULL ||
        strstr(stderr_buf, "job_id=42") == NULL ||
        strstr(stderr_buf, "event=submit_accept") == NULL ||
        strstr(stderr_buf, "event=completion") == NULL ||
        strstr(stderr_buf, "task_kind=nop") == NULL ||
        strstr(stderr_buf, task_id_field) == NULL) {
        return 1;
    }

    return 0;
}
