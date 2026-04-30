#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iouringd/client.h"
#include "iouringd/submit.h"

int main(int argc, char **argv)
{
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_task_id_record_v1 task;
    int fd;

    if (argc != 2) {
        fprintf(stderr, "usage: %s SOCKET_PATH\n", argv[0]);
        return 2;
    }

    fd = iouringd_client_connect(argv[1]);
    if (fd < 0) {
        perror("connect");
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(fd, &handshake) != 0) {
        perror("handshake");
        close(fd);
        return 1;
    }

    memset(&task, 0, sizeof(task));
    if (iouringd_submit_nop(fd, &task) != 0) {
        perror("submit_nop");
        close(fd);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(fd, &completion) != 0) {
        perror("wait_completion");
        close(fd);
        return 1;
    }

    printf("task=%llu result=%d\n",
           (unsigned long long)completion.task.task_id,
           completion.res);
    close(fd);
    return 0;
}
