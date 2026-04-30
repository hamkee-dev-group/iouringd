#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

int main(void)
{
    struct iouringd_completion_record_v1 completion;
    int fds[2];
    pid_t child;
    int status;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return 1;
    }

    child = fork();
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        return 1;
    }

    if (child == 0) {
        close(fds[0]);
        memset(&completion, 0, sizeof(completion));
        completion.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
        completion.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
        completion.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
        completion.task.task_id = IOURINGD_TASK_ID_RESERVED_MIN;
        completion.task_kind.value = IOURINGD_TASK_KIND_NOP;

        if (write_full(fds[1], &completion, sizeof(completion)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    memset(&completion, 0, sizeof(completion));
    errno = 0;
    if (iouringd_wait_completion(fds[0], &completion) != -1 || errno != EPROTO) {
        close(fds[0]);
        waitpid(child, &status, 0);
        return 1;
    }

    close(fds[0]);
    if (waitpid(child, &status, 0) != child) {
        return 1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return 1;
    }

    return 0;
}
