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
    struct iouringd_completion_record_v1 first_completion;
    struct iouringd_completion_record_v1 second_completion;
    struct iouringd_handshake_result_v1 first_handshake;
    struct iouringd_handshake_result_v1 second_handshake;
    struct iouringd_task_id_record_v1 first_task;
    struct iouringd_task_id_record_v1 second_task;
    char socket_path[108];
    pid_t child;
    int first_fd;
    int second_fd;
    int status;

    if (argc != 2) {
        return 2;
    }

    alarm(10);
    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-two-clients-timeout-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    child = fork();
    if (child < 0) {
        return 1;
    }

    if (child == 0) {
        execl(argv[1], argv[1], socket_path, (char *)NULL);
        _exit(127);
    }

    first_fd = wait_for_connect(socket_path, child);
    if (first_fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    second_fd = wait_for_connect(socket_path, child);
    if (second_fd < 0) {
        close(first_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&first_handshake, 0, sizeof(first_handshake));
    memset(&second_handshake, 0, sizeof(second_handshake));
    if (iouringd_client_handshake_fd(first_fd, &first_handshake) != 0 ||
        iouringd_client_handshake_fd(second_fd, &second_handshake) != 0) {
        close(first_fd);
        close(second_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    if ((first_handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_TIMEOUT) == 0u ||
        (second_handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_TIMEOUT) == 0u) {
        close(first_fd);
        close(second_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&first_task, 0, sizeof(first_task));
    memset(&second_task, 0, sizeof(second_task));
    if (iouringd_submit_timeout(first_fd, UINT64_C(50000000), &first_task) != 0 ||
        iouringd_submit_timeout(second_fd, UINT64_C(50000000), &second_task) != 0) {
        close(first_fd);
        close(second_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&first_completion, 0, sizeof(first_completion));
    memset(&second_completion, 0, sizeof(second_completion));
    if (iouringd_wait_completion(first_fd, &first_completion) != 0 ||
        iouringd_wait_completion(second_fd, &second_completion) != 0) {
        close(first_fd);
        close(second_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    close(first_fd);
    close(second_fd);

    if (first_task.task_id == second_task.task_id ||
        second_task.task_id != first_task.task_id + 1u ||
        first_completion.task.task_id != first_task.task_id ||
        second_completion.task.task_id != second_task.task_id ||
        first_completion.task_kind.value != IOURINGD_TASK_KIND_TIMEOUT ||
        second_completion.task_kind.value != IOURINGD_TASK_KIND_TIMEOUT ||
        first_completion.res != -ETIME ||
        second_completion.res != -ETIME) {
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
