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

int main(int argc, char **argv)
{
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_id_record_v1 listener_resource;
    struct iouringd_stats_result_v1 stats;
    struct iouringd_submit_result_v1 submit_result;
    struct sockaddr_un listen_addr;
    char daemon_socket_path[108];
    char listen_socket_path[108];
    pid_t child;
    int control_fd = -1;
    int listen_fd = -1;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(daemon_socket_path,
             sizeof(daemon_socket_path),
             "/tmp/iouringd-accept-einval-%ld.sock",
             (long)getpid());
    snprintf(listen_socket_path,
             sizeof(listen_socket_path),
             "/tmp/iouringd-accept-einval-listener-%ld.sock",
             (long)getpid());
    unlink(daemon_socket_path);
    unlink(listen_socket_path);

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0 || set_cloexec(listen_fd) != 0) {
        if (listen_fd >= 0) {
            close(listen_fd);
        }
        unlink(listen_socket_path);
        return 1;
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sun_family = AF_UNIX;
    memcpy(listen_addr.sun_path,
           listen_socket_path,
           strlen(listen_socket_path) + 1u);
    if (bind(listen_fd, (const struct sockaddr *)&listen_addr, sizeof(listen_addr)) != 0 ||
        listen(listen_fd, 1) != 0) {
        close(listen_fd);
        unlink(listen_socket_path);
        return 1;
    }

    if (setenv("IOURINGD_TEST_FAULT", "submit_accept_einval", 1) != 0) {
        close(listen_fd);
        unlink(listen_socket_path);
        return 1;
    }

    child = fork();
    if (child < 0) {
        unsetenv("IOURINGD_TEST_FAULT");
        close(listen_fd);
        unlink(listen_socket_path);
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
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_ACCEPT) == 0u ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_NOP) == 0u ||
        handshake.capabilities.registered_fd_slots < 2u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&listener_resource, 0, sizeof(listener_resource));
    if (iouringd_register_fd(control_fd, listen_fd, &listener_resource) != 0 ||
        listener_resource.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&submit_result, 0, sizeof(submit_result));
    errno = 0;
    if (iouringd_submit_accept_result(control_fd,
                                      listener_resource.resource_id,
                                      (uint32_t)sizeof(listen_addr),
                                      &submit_result) != -1 ||
        errno != EINVAL ||
        submit_result.task.task_id != IOURINGD_TASK_ID_INVALID ||
        submit_result.res != -EINVAL) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&stats, 0, sizeof(stats));
    if (iouringd_get_stats(control_fd, &stats) != 0 ||
        stats.outstanding_tasks != 0u ||
        stats.outstanding_credit_tasks != 0u ||
        stats.accepted_submits != 0u ||
        stats.rejected_submits != 1u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&submit_result, 0, sizeof(submit_result));
    if (iouringd_submit_nop_result(control_fd, &submit_result) != 0 ||
        submit_result.task.task_id == IOURINGD_TASK_ID_INVALID ||
        submit_result.res != IOURINGD_COMPLETION_RES_OK) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != submit_result.task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_NOP ||
        completion.res != IOURINGD_COMPLETION_RES_OK) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(listen_fd);
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    close(control_fd);
    close(listen_fd);

    kill(child, SIGTERM);
    if (waitpid(child, &status, 0) != child) {
        unlink(daemon_socket_path);
        unlink(listen_socket_path);
        return 1;
    }

    unlink(daemon_socket_path);
    unlink(listen_socket_path);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        return 1;
    }

    return 0;
}
