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

static int exercise_completion(struct iouringd_completion_record_v1 *completion)
{
    int fds[2];
    pid_t child;
    int status;
    int rc;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return -1;
    }

    child = fork();
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (child == 0) {
        close(fds[0]);

        if (write_full(fds[1], completion, sizeof(*completion)) != 0) {
            close(fds[1]);
            _exit(1);
        }

        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    errno = 0;
    rc = iouringd_wait_completion(fds[0], completion);
    close(fds[0]);

    if (waitpid(child, &status, 0) != child) {
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }

    return rc;
}

static void init_valid_completion(struct iouringd_completion_record_v1 *completion)
{
    memset(completion, 0, sizeof(*completion));
    completion->header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    completion->header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    completion->header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    completion->task.task_id = 1;
    completion->task_kind.value = IOURINGD_TASK_KIND_NOP;
}

int main(void)
{
    struct iouringd_completion_record_v1 completion;

    init_valid_completion(&completion);
    completion.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC ^ UINT32_C(0xFF);
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.header.version_major =
        (uint16_t)(IOURINGD_PROTOCOL_V1_MAJOR + 1u);
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task.task_id = IOURINGD_TASK_ID_INVALID;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task.task_id = IOURINGD_TASK_ID_RESERVED_MIN;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task.task_id = IOURINGD_TASK_ID_RESERVED_MIN + UINT64_C(1);
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task.task_id = IOURINGD_TASK_ID_RESERVED_MAX;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task_kind.value = 0;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task_kind.value = IOURINGD_TASK_KIND_MAX_V1;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task_kind.value = IOURINGD_TASK_KIND_SOCK_READ;
    completion.res = 4;
    completion.payload_length = 3;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task_kind.value = IOURINGD_TASK_KIND_FILE_READ;
    completion.res = 4;
    completion.payload_length = 3;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task_kind.value = IOURINGD_TASK_KIND_ACCEPT;
    completion.res = 0;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.task_kind.value = IOURINGD_TASK_KIND_ACCEPT;
    completion.res = 7;
    if (exercise_completion(&completion) != 0) {
        return 1;
    }

    init_valid_completion(&completion);
    completion.header.version_minor =
        (uint16_t)(IOURINGD_PROTOCOL_V1_MINOR + 1u);
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    return 0;
}
