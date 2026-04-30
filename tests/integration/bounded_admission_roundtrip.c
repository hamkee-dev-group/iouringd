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
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_submit_result_v1 submit_result;
    iouringd_task_id_t task_ids[8];
    uint32_t accepted;
    uint32_t index;
    uint32_t timeout_seen = 0u;
    uint32_t nop_seen = 0u;
    char socket_path[108];
    pid_t child;
    int fd;
    int status;

    if (argc != 2) {
        return 2;
    }

    alarm(10);
    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-bounded-admission-%ld.sock",
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

    fd = wait_for_connect(socket_path, child);
    if (fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(fd, &handshake) != 0) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    accepted = handshake.capabilities.submit_credits;
    if ((handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_TIMEOUT) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_NOP) == 0u ||
        accepted == 0u ||
        accepted >= (uint32_t)(sizeof(task_ids) / sizeof(task_ids[0]))) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    for (index = 0; index < accepted; ++index) {
        memset(&submit_result, 0, sizeof(submit_result));
        if (iouringd_submit_timeout_result(fd,
                                           UINT64_C(100000000),
                                           &submit_result) != 0) {
            close(fd);
            kill(child, SIGTERM);
            waitpid(child, &status, 0);
            unlink(socket_path);
            return 1;
        }

        task_ids[index] = submit_result.task.task_id;
    }

    memset(&submit_result, 0, sizeof(submit_result));
    errno = 0;
    if (iouringd_submit_nop_result(fd, &submit_result) != -1 ||
        errno != EAGAIN ||
        submit_result.task.task_id != IOURINGD_TASK_ID_INVALID ||
        submit_result.res != -EAGAIN ||
        submit_result.credits != 0u) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(fd, &completion) != 0 ||
        completion.task_kind.value != IOURINGD_TASK_KIND_TIMEOUT ||
        completion.res != -ETIME) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }
    timeout_seen += 1u;

    memset(&submit_result, 0, sizeof(submit_result));
    if (iouringd_submit_nop_result(fd, &submit_result) != 0 ||
        submit_result.task.task_id == IOURINGD_TASK_ID_INVALID) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }
    task_ids[accepted] = submit_result.task.task_id;

    for (index = 0; index < accepted; ++index) {
        memset(&completion, 0, sizeof(completion));
        if (iouringd_wait_completion(fd, &completion) != 0) {
            close(fd);
            kill(child, SIGTERM);
            waitpid(child, &status, 0);
            unlink(socket_path);
            return 1;
        }

        if (completion.task.task_id == task_ids[accepted]) {
            if (completion.task_kind.value != IOURINGD_TASK_KIND_NOP ||
                completion.res != 0) {
                close(fd);
                kill(child, SIGTERM);
                waitpid(child, &status, 0);
                unlink(socket_path);
                return 1;
            }
            nop_seen += 1u;
            continue;
        }

        if (completion.task_kind.value != IOURINGD_TASK_KIND_TIMEOUT ||
            completion.res != -ETIME) {
            close(fd);
            kill(child, SIGTERM);
            waitpid(child, &status, 0);
            unlink(socket_path);
            return 1;
        }
        timeout_seen += 1u;
    }

    close(fd);

    if (timeout_seen != accepted || nop_seen != 1u) {
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
