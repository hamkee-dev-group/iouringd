#define _POSIX_C_SOURCE 200809L

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

static int write_full(int fd, const void *buf, size_t len)
{
    size_t offset = 0;
    const char *bytes = buf;

    while (offset < len) {
        ssize_t rc = write(fd, bytes + offset, len - offset);

        if (rc <= 0) {
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

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

static int complete_nop_session(int fd, uint64_t *task_id)
{
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_task_id_record_v1 task;

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(fd, &handshake) != 0) {
        return -1;
    }

    if ((handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_NOP) == 0u) {
        return -1;
    }

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_nop(fd, &task) != 0) {
        return -1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(fd, &completion) != 0) {
        return -1;
    }

    if (completion.task.task_id != task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_NOP ||
        completion.res != 0) {
        return -1;
    }

    *task_id = task.task_id;
    return 0;
}

int main(int argc, char **argv)
{
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_submit_request_v1 request;
    char socket_path[108];
    uint64_t task_id;
    char byte;
    pid_t child;
    int fd;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-malformed-timeout-then-nop-%ld.sock",
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

    if ((handshake.capabilities.op_mask & IOURINGD_CAPABILITY_OP_TIMEOUT) == 0u) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    memset(&request, 0, sizeof(request));
    request.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    request.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    request.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    request.task_kind.value = IOURINGD_TASK_KIND_TIMEOUT;

    if (write_full(fd, &request, sizeof(request)) != 0 ||
        shutdown(fd, SHUT_WR) != 0) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    if (read(fd, &byte, sizeof(byte)) != 0) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    close(fd);

    fd = wait_for_connect(socket_path, child);
    if (fd < 0) {
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    if (complete_nop_session(fd, &task_id) != 0) {
        close(fd);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    close(fd);

    if (task_id != 1u) {
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
