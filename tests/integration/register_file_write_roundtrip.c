#define _POSIX_C_SOURCE 200809L

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

static int read_full(int fd, void *buf, size_t len)
{
    size_t offset = 0;
    unsigned char *bytes = buf;

    while (offset < len) {
        ssize_t rc = read(fd, bytes + offset, len - offset);

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
    static const char message[] = "file-write";
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_task_id_record_v1 task;
    struct iouringd_resource_id_record_v1 resource;
    char file_template[] = "/tmp/iouringd-file-write-XXXXXX";
    char file_contents[sizeof(message)];
    char socket_path[108];
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
             "/tmp/iouringd-file-write-roundtrip-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    child = fork();
    if (child < 0) {
        close(file_fd);
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
        close(file_fd);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(control_fd, &handshake) != 0 ||
        (handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_WRITEV) == 0u ||
        handshake.capabilities.registered_fd_slots == 0u ||
        handshake.capabilities.io_bytes_max < sizeof(message) - 1u) {
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

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_file_write(control_fd,
                                   resource.resource_id,
                                   message,
                                   (uint32_t)(sizeof(message) - 1u),
                                   &task) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(file_fd);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(control_fd, &completion) != 0 ||
        completion.task.task_id != task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_FILE_WRITE ||
        completion.res != (int32_t)(sizeof(message) - 1u) ||
        completion.payload_length != 0u) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(file_fd);
        return 1;
    }

    if (lseek(file_fd, 0, SEEK_SET) < 0 ||
        read_full(file_fd, file_contents, sizeof(message) - 1u) != 0 ||
        memcmp(file_contents, message, sizeof(message) - 1u) != 0) {
        close(control_fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        close(file_fd);
        return 1;
    }

    close(control_fd);
    close(file_fd);

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
