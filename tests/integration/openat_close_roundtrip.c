#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
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
    struct iouringd_task_id_record_v1 task;
    iouringd_resource_id_t resource_id;
    char socket_path[108];
    char file_path[108];
    pid_t child;
    int control_fd = -1;
    int file_fd = -1;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-openat-close-%ld.sock",
             (long)getpid());
    snprintf(file_path,
             sizeof(file_path),
             "/tmp/iouringd-openat-close-%ld.txt",
             (long)getpid());
    unlink(socket_path);
    unlink(file_path);

    file_fd = open(file_path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (file_fd < 0) {
        return 1;
    }
    close(file_fd);
    file_fd = -1;

    child = fork();
    if (child < 0) {
        unlink(file_path);
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
        unlink(file_path);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_OPENAT) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_CLOSE) == 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_openat(control_fd,
                               file_path,
                               O_RDONLY | O_CLOEXEC,
                               0u,
                               &task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_OPENAT ||
        completion.res <= 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }
    resource_id = (iouringd_resource_id_t)completion.res;

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_close(control_fd, resource_id, &task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_CLOSE ||
        completion.res != 0 ||
        completion.payload_length != 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    memset(&submit_result, 0, sizeof(submit_result));
    if (iouringd_submit_file_read_result(control_fd,
                                         resource_id,
                                         UINT32_C(1),
                                         &submit_result) != -1 ||
        errno != ENOENT ||
        submit_result.task.task_id != IOURINGD_TASK_ID_INVALID ||
        submit_result.res != -ENOENT) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    close(control_fd);

    kill(child, SIGTERM);
    if (waitpid(child, &status, 0) != child) {
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    unlink(socket_path);
    unlink(file_path);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        return 1;
    }

    return 0;
}
