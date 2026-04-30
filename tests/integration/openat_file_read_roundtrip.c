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

int main(int argc, char **argv)
{
    static const char message[] = "opened-by-daemon";
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_task_id_record_v1 open_task;
    struct iouringd_task_id_record_v1 read_task;
    struct iouringd_resource_id_record_v1 released;
    iouringd_resource_id_t open_resource_id;
    char socket_path[108];
    char file_path[108];
    char payload[sizeof(message)];
    size_t payload_length = 0u;
    pid_t child;
    int control_fd = -1;
    int file_fd = -1;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-openat-%ld.sock",
             (long)getpid());
    snprintf(file_path,
             sizeof(file_path),
             "/tmp/iouringd-openat-%ld.txt",
             (long)getpid());
    unlink(socket_path);
    unlink(file_path);

    file_fd = open(file_path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (file_fd < 0) {
        return 1;
    }

    if (write_full(file_fd, message, sizeof(message) - 1u) != 0) {
        close(file_fd);
        unlink(file_path);
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
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_READV) == 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    memset(&open_task, 0, sizeof(open_task));
    if (iouringd_submit_openat(control_fd,
                               file_path,
                               O_RDONLY | O_CLOEXEC,
                               0u,
                               &open_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != open_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_OPENAT ||
        completion.res <= 0 ||
        completion.payload_length != 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }
    open_resource_id = (iouringd_resource_id_t)completion.res;

    memset(&read_task, 0, sizeof(read_task));
    if (iouringd_submit_file_read(control_fd,
                                  open_resource_id,
                                  (uint32_t)(sizeof(message) - 1u),
                                  &read_task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != read_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_FILE_READ ||
        completion.res != (int32_t)(sizeof(message) - 1u) ||
        completion.payload_length != sizeof(message) - 1u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        unlink(file_path);
        return 1;
    }

    memset(payload, 0, sizeof(payload));
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
        unlink(file_path);
        return 1;
    }

    if (iouringd_release_resource(control_fd,
                                  open_resource_id,
                                  &released) != 0 ||
        released.resource_id != open_resource_id) {
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
