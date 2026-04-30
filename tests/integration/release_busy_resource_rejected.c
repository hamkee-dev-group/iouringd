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

static int write_full(int fd, const void *buf, size_t len)
{
    size_t offset = 0;
    const unsigned char *bytes = buf;

    while (offset < len) {
        ssize_t rc = write(fd, bytes + offset, len - offset);

        if (rc <= 0) {
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
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
    static const char message[] = "busy";
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_result_v1 release_result;
    struct iouringd_resource_id_record_v1 replacement;
    struct iouringd_resource_id_record_v1 resource;
    struct iouringd_task_id_record_v1 task;
    char payload[sizeof(message)];
    char socket_path[108];
    size_t payload_length;
    pid_t child;
    int control_fd;
    int first_pair[2];
    int second_pair[2];
    int status;

    if (argc != 2) {
        return 2;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, first_pair) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, second_pair) != 0) {
        return 1;
    }
    if (set_cloexec(first_pair[0]) != 0 || set_cloexec(first_pair[1]) != 0 ||
        set_cloexec(second_pair[0]) != 0 || set_cloexec(second_pair[1]) != 0) {
        close(first_pair[0]);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-release-busy-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    child = fork();
    if (child < 0) {
        close(first_pair[0]);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
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
        close(first_pair[0]);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_READV) == 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(first_pair[0]);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }

    memset(&resource, 0, sizeof(resource));
    if (iouringd_register_fd(control_fd, first_pair[0], &resource) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(first_pair[0]);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }
    close(first_pair[0]);
    first_pair[0] = -1;

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_sock_read(control_fd,
                                  resource.resource_id,
                                  (uint32_t)(sizeof(message) - 1u),
                                  &task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }

    memset(&release_result, 0, sizeof(release_result));
    if (iouringd_release_resource_result(control_fd,
                                         resource.resource_id,
                                         &release_result) != -1 ||
        errno != EBUSY ||
        release_result.resource.resource_id != IOURINGD_RESOURCE_ID_INVALID ||
        release_result.res != -EBUSY) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }

    if (write_full(first_pair[1], message, sizeof(message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_SOCK_READ ||
        completion.res != (int32_t)(sizeof(message) - 1u) ||
        completion.payload_length != sizeof(message) - 1u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }

    memset(payload, 0, sizeof(payload));
    payload_length = 0u;
    if (iouringd_read_completion_payload(control_fd,
                                         &completion,
                                         payload,
                                         sizeof(payload),
                                         &payload_length) != 0 ||
        payload_length != sizeof(message) - 1u ||
        memcmp(payload, message, sizeof(message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }

    if (iouringd_release_resource(control_fd, resource.resource_id, &resource) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }

    memset(&replacement, 0, sizeof(replacement));
    if (iouringd_register_fd(control_fd, second_pair[0], &replacement) != 0 ||
        replacement.resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(first_pair[1]);
        close(second_pair[0]);
        close(second_pair[1]);
        return 1;
    }
    close(second_pair[0]);
    second_pair[0] = -1;

    close(control_fd);
    close(first_pair[1]);
    close(second_pair[1]);

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
