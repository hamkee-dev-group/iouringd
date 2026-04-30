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
    struct iouringd_task_id_record_v1 task;
    char socket_path[108];
    pid_t child;
    int fd;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-config-stats-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    child = fork();
    if (child < 0) {
        return 1;
    }

    if (child == 0) {
        execl(argv[1],
              argv[1],
              "--ring-entries",
              "8",
              "--max-clients",
              "2",
              "--registered-fds",
              "3",
              "--registered-buffers",
              "1",
              "--per-client-credits",
              "2",
              "--io-bytes-max",
              "128",
              socket_path,
              (char *)NULL);
        _exit(127);
    }

    fd = wait_for_connect(socket_path, child);
    if (fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(fd, &handshake) != 0 ||
        handshake.capabilities.ring_entries < 8u ||
        handshake.capabilities.submit_credits + 1u != handshake.capabilities.ring_entries ||
        handshake.capabilities.registered_fd_slots != 3u ||
        handshake.capabilities.registered_buffer_slots != 1u ||
        handshake.capabilities.io_bytes_max != 128u) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&stats, 0, sizeof(stats));
    if (iouringd_get_stats(fd, &stats) != 0 ||
        stats.active_clients != 1u ||
        stats.outstanding_tasks != 0u ||
        stats.outstanding_credit_tasks != 0u ||
        stats.available_credits != handshake.capabilities.submit_credits ||
        stats.registered_files != 0u ||
        stats.registered_buffers != 0u ||
        stats.accepted_submits != 0u ||
        stats.rejected_submits != 0u ||
        stats.completions != 0u ||
        stats.per_client_credit_limit != 2u ||
        stats.clients_at_credit_limit != 0u) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_nop(fd, &task) != 0) {
        close(fd);
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
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&stats, 0, sizeof(stats));
    if (iouringd_get_stats(fd, &stats) != 0 ||
        stats.active_clients != 1u ||
        stats.outstanding_tasks != 0u ||
        stats.outstanding_credit_tasks != 0u ||
        stats.accepted_submits != 1u ||
        stats.rejected_submits != 0u ||
        stats.completions != 1u ||
        stats.per_client_credit_limit != 2u ||
        stats.clients_at_credit_limit != 0u) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    close(fd);

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
