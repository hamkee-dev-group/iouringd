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
    struct iouringd_stats_result_v1 stats;
    struct iouringd_task_id_record_v1 first_task;
    struct iouringd_task_id_record_v1 second_task;
    char socket_path[108];
    pid_t child;
    int control_fd;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-enter-publish-recovery-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    if (setenv("IOURINGD_TEST_FAULT", "enter_after_publish_once", 1) != 0) {
        return 1;
    }

    child = fork();
    if (child < 0) {
        unsetenv("IOURINGD_TEST_FAULT");
        return 1;
    }

    if (child == 0) {
        execl(argv[1], argv[1], socket_path, (char *)NULL);
        _exit(127);
    }
    unsetenv("IOURINGD_TEST_FAULT");

    control_fd = wait_for_connect(socket_path, child);
    if (control_fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_NOP) == 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&first_task, 0, sizeof(first_task));
    if (iouringd_submit_nop(control_fd, &first_task) != 0 ||
        first_task.task_id == IOURINGD_TASK_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != first_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_NOP ||
        completion.res != IOURINGD_COMPLETION_RES_OK) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&stats, 0, sizeof(stats));
    if (iouringd_get_stats(control_fd, &stats) != 0 ||
        stats.outstanding_tasks != 0u ||
        stats.outstanding_credit_tasks != 0u ||
        stats.accepted_submits != UINT64_C(1) ||
        stats.rejected_submits != 0u ||
        stats.completions != UINT64_C(1)) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&second_task, 0, sizeof(second_task));
    if (iouringd_submit_nop(control_fd, &second_task) != 0 ||
        second_task.task_id == IOURINGD_TASK_ID_INVALID ||
        second_task.task_id == first_task.task_id) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != second_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_NOP ||
        completion.res != IOURINGD_COMPLETION_RES_OK) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    close(control_fd);

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
