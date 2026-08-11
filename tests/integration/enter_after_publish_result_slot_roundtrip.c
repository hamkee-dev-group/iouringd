#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
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

static int stranded_task_is_tracked(int control_fd,
                                    const struct iouringd_task_id_record_v1 *task,
                                    uint64_t expected_accepted)
{
    struct iouringd_stats_result_v1 stats;

    if (task->task_id == IOURINGD_TASK_ID_INVALID) {
        return -1;
    }

    memset(&stats, 0, sizeof(stats));
    if (iouringd_get_stats(control_fd, &stats) != 0 ||
        stats.outstanding_tasks != 1u ||
        stats.outstanding_credit_tasks != 1u ||
        stats.accepted_submits != expected_accepted ||
        stats.rejected_submits != 0u) {
        return -1;
    }

    return 0;
}

static int flush_and_collect(int control_fd,
                             struct iouringd_completion_record_v1 *records,
                             struct iouringd_task_id_record_v1 *nop_task)
{
    size_t index;

    memset(nop_task, 0, sizeof(*nop_task));
    if (iouringd_submit_nop(control_fd, nop_task) != 0 ||
        nop_task->task_id == IOURINGD_TASK_ID_INVALID) {
        return -1;
    }

    for (index = 0; index < 2u; ++index) {
        memset(&records[index], 0, sizeof(records[index]));
        if (iouringd_wait_completion(control_fd, &records[index]) != 0) {
            return -1;
        }
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

static int nop_completed(const struct iouringd_completion_record_v1 *records,
                         const struct iouringd_task_id_record_v1 *nop_task)
{
    const struct iouringd_completion_record_v1 *matched =
        find_completion(records, nop_task->task_id);

    if (matched == NULL || matched->task_kind.value != IOURINGD_TASK_KIND_NOP ||
        matched->res != IOURINGD_COMPLETION_RES_OK) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct iouringd_completion_record_v1 records[2];
    const struct iouringd_completion_record_v1 *matched;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_id_record_v1 accepted_resource;
    struct iouringd_resource_id_record_v1 listener_resource;
    struct iouringd_resource_id_record_v1 opened_resource;
    struct iouringd_stats_result_v1 stats;
    struct iouringd_task_id_record_v1 nop_task;
    struct iouringd_task_id_record_v1 op_task;
    struct sockaddr_un listen_addr;
    char file_template[] = "/tmp/iouringd-enter-publish-openat-XXXXXX";
    char daemon_socket_path[108];
    char listen_socket_path[108];
    pid_t child;
    int control_fd;
    int file_fd;
    int listen_fd;
    int peer_fd;
    int status;

    if (argc != 2) {
        return 2;
    }

    file_fd = mkstemp(file_template);
    if (file_fd < 0) {
        return 1;
    }
    close(file_fd);

    snprintf(daemon_socket_path,
             sizeof(daemon_socket_path),
             "/tmp/iouringd-enter-publish-slot-%ld.sock",
             (long)getpid());
    snprintf(listen_socket_path,
             sizeof(listen_socket_path),
             "/tmp/iouringd-enter-publish-slot-listener-%ld.sock",
             (long)getpid());
    unlink(daemon_socket_path);
    unlink(listen_socket_path);

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    peer_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0 || peer_fd < 0 || set_cloexec(listen_fd) != 0 ||
        set_cloexec(peer_fd) != 0) {
        if (listen_fd >= 0) {
            close(listen_fd);
        }
        if (peer_fd >= 0) {
            close(peer_fd);
        }
        unlink(file_template);
        return 1;
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sun_family = AF_UNIX;
    memcpy(listen_addr.sun_path, listen_socket_path, strlen(listen_socket_path) + 1u);
    if (bind(listen_fd, (const struct sockaddr *)&listen_addr, sizeof(listen_addr)) != 0 ||
        listen(listen_fd, 1) != 0 ||
        connect(peer_fd, (const struct sockaddr *)&listen_addr, sizeof(listen_addr)) != 0) {
        close(listen_fd);
        close(peer_fd);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    if (setenv("IOURINGD_TEST_FAULT", "enter_after_publish", 1) != 0) {
        close(listen_fd);
        close(peer_fd);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    child = fork();
    if (child < 0) {
        unsetenv("IOURINGD_TEST_FAULT");
        close(listen_fd);
        close(peer_fd);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    if (child == 0) {
        execl(argv[1], argv[1], daemon_socket_path, (char *)NULL);
        _exit(127);
    }
    unsetenv("IOURINGD_TEST_FAULT");

    control_fd = wait_for_connect(daemon_socket_path, child);
    if (control_fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_ACCEPT) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_OPENAT) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_NOP) == 0u ||
        handshake.capabilities.registered_fd_slots < 2u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    memset(&listener_resource, 0, sizeof(listener_resource));
    if (iouringd_register_fd(control_fd, listen_fd, &listener_resource) != 0 ||
        listener_resource.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    memset(&op_task, 0, sizeof(op_task));
    if (iouringd_submit_accept(control_fd,
                               listener_resource.resource_id,
                               0u,
                               &op_task) != 0 ||
        stranded_task_is_tracked(control_fd, &op_task, UINT64_C(1)) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    if (flush_and_collect(control_fd, records, &nop_task) != 0 ||
        nop_completed(records, &nop_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    matched = find_completion(records, op_task.task_id);
    if (matched == NULL || matched->task_kind.value != IOURINGD_TASK_KIND_ACCEPT ||
        matched->res <= 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    memset(&accepted_resource, 0, sizeof(accepted_resource));
    accepted_resource.resource_id = (iouringd_resource_id_t)matched->res;
    if (iouringd_release_resource(control_fd,
                                  accepted_resource.resource_id,
                                  &accepted_resource) != 0 ||
        iouringd_release_resource(control_fd,
                                  listener_resource.resource_id,
                                  &listener_resource) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    memset(&op_task, 0, sizeof(op_task));
    if (iouringd_submit_openat(control_fd, file_template, O_RDONLY, 0u, &op_task) != 0 ||
        stranded_task_is_tracked(control_fd, &op_task, UINT64_C(3)) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    if (flush_and_collect(control_fd, records, &nop_task) != 0 ||
        nop_completed(records, &nop_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    matched = find_completion(records, op_task.task_id);
    if (matched == NULL || matched->task_kind.value != IOURINGD_TASK_KIND_OPENAT ||
        matched->res <= 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    memset(&stats, 0, sizeof(stats));
    if (iouringd_get_stats(control_fd, &stats) != 0 ||
        stats.outstanding_tasks != 0u ||
        stats.outstanding_credit_tasks != 0u ||
        stats.accepted_submits != UINT64_C(4) ||
        stats.rejected_submits != 0u ||
        stats.completions != UINT64_C(4) ||
        stats.registered_files != 1u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    memset(&opened_resource, 0, sizeof(opened_resource));
    opened_resource.resource_id = (iouringd_resource_id_t)matched->res;
    if (iouringd_release_resource(control_fd,
                                  opened_resource.resource_id,
                                  &opened_resource) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        close(peer_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    close(control_fd);
    close(listen_fd);
    close(peer_fd);

    kill(child, SIGTERM);
    if (waitpid(child, &status, 0) != child) {
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        unlink(file_template);
        return 1;
    }

    unlink(daemon_socket_path);
    unlink(listen_socket_path);
    unlink(file_template);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        return 1;
    }

    return 0;
}
