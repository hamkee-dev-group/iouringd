#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
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

static void init_completion(struct iouringd_completion_record_v1 *completion)
{
    memset(completion, 0, sizeof(*completion));
    completion->header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    completion->header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    completion->header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    completion->task.task_id = 1;
    completion->task_kind.value = IOURINGD_TASK_KIND_NOP;
}

static int exercise_completion(const struct iouringd_completion_record_v1 *input)
{
    struct iouringd_completion_record_v1 completion;
    int fds[2];
    int rc;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return -1;
    }

    if (write_full(fds[1], input, sizeof(*input)) != 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    close(fds[1]);
    memset(&completion, 0, sizeof(completion));
    errno = 0;
    rc = iouringd_wait_completion(fds[0], &completion);
    close(fds[0]);
    return rc;
}

int main(void)
{
    struct iouringd_completion_record_v1 completion;

    init_completion(&completion);
    completion.task.task_id = IOURINGD_TASK_ID_INVALID;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    init_completion(&completion);
    completion.payload_length = 1;
    if (exercise_completion(&completion) != -1 || errno != EPROTO) {
        return 1;
    }

    return 0;
}
