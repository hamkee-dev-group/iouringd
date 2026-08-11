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

static int set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);

    if (flags < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int closed_resource_is_unusable(int control_fd,
                                       iouringd_resource_id_t resource_id)
{
    struct iouringd_submit_result_v1 submit_result;

    memset(&submit_result, 0, sizeof(submit_result));
    errno = 0;
    if (iouringd_submit_file_read_result(control_fd,
                                         resource_id,
                                         UINT32_C(1),
                                         &submit_result) != -1 ||
        errno != ENOENT ||
        submit_result.task.task_id != IOURINGD_TASK_ID_INVALID ||
        submit_result.res != -ENOENT) {
        return -1;
    }

    return 0;
}

static const struct iouringd_completion_record_v1 *find_completion(
    const struct iouringd_completion_record_v1 *records,
    iouringd_task_id_t task_id)
{
    size_t index;

    for (index = 0; index < 2u; ++index) {
        if (records[index].task.task_id == task_id) {
            return &records[index];
        }
    }

    return NULL;
}

int main(int argc, char **argv)
{
    struct iouringd_completion_record_v1 records[2];
    const struct iouringd_completion_record_v1 *matched;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_id_record_v1 resource;
    struct iouringd_resource_id_record_v1 released;
    struct iouringd_stats_result_v1 stats;
    struct iouringd_task_id_record_v1 close_task;
    struct iouringd_task_id_record_v1 nop_task;
    char file_template[] = "/tmp/iouringd-enter-publish-close-XXXXXX";
    char socket_path[108];
    size_t index;
    pid_t child;
    int control_fd;
    int file_fd;
    int status;

    if (argc != 2) {
        return 2;
    }

    file_fd = mkstemp(file_template);
    if (file_fd < 0) {
        return 1;
    }
    unlink(file_template);
    if (set_cloexec(file_fd) != 0) {
        close(file_fd);
        return 1;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-enter-publish-close-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    if (setenv("IOURINGD_TEST_FAULT", "enter_after_publish", 1) != 0) {
        close(file_fd);
        return 1;
    }

    child = fork();
    if (child < 0) {
        unsetenv("IOURINGD_TEST_FAULT");
        close(file_fd);
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
        close(file_fd);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_CLOSE) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_NOP) == 0u ||
        handshake.capabilities.registered_fd_slots == 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(file_fd);
        return 1;
    }

    memset(&resource, 0, sizeof(resource));
    if (iouringd_register_fd(control_fd, file_fd, &resource) != 0 ||
        resource.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(file_fd);
        return 1;
    }
    close(file_fd);
    file_fd = -1;

    memset(&close_task, 0, sizeof(close_task));
    if (iouringd_submit_close(control_fd, resource.resource_id, &close_task) != 0 ||
        close_task.task_id == IOURINGD_TASK_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&stats, 0, sizeof(stats));
    if (iouringd_get_stats(control_fd, &stats) != 0 ||
        stats.outstanding_tasks != 1u ||
        stats.outstanding_credit_tasks != 1u ||
        stats.accepted_submits != UINT64_C(1) ||
        stats.rejected_submits != 0u ||
        stats.registered_files != 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    if (closed_resource_is_unusable(control_fd, resource.resource_id) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&nop_task, 0, sizeof(nop_task));
    if (iouringd_submit_nop(control_fd, &nop_task) != 0 ||
        nop_task.task_id == IOURINGD_TASK_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    for (index = 0; index < 2u; ++index) {
        memset(&records[index], 0, sizeof(records[index]));
        if (iouringd_wait_completion(control_fd, &records[index]) != 0) {
            close(control_fd);
            kill(child, SIGTERM);
            waitpid(child, &status, 0);
            unlink(socket_path);
            return 1;
        }
    }

    matched = find_completion(records, close_task.task_id);
    if (matched == NULL || matched->task_kind.value != IOURINGD_TASK_KIND_CLOSE ||
        matched->res != 0 || matched->payload_length != 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    matched = find_completion(records, nop_task.task_id);
    if (matched == NULL || matched->task_kind.value != IOURINGD_TASK_KIND_NOP ||
        matched->res != IOURINGD_COMPLETION_RES_OK) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    if (closed_resource_is_unusable(control_fd, resource.resource_id) != 0) {
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
        stats.accepted_submits != UINT64_C(2) ||
        stats.rejected_submits != UINT64_C(2) ||
        stats.completions != UINT64_C(2) ||
        stats.registered_files != 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&released, 0, sizeof(released));
    if (iouringd_release_resource(control_fd, resource.resource_id, &released) == 0) {
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
