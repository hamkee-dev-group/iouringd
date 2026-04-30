#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iouringd/client.h"
#include "iouringd/submit.h"

static int read_payload(int fd,
                        const struct iouringd_completion_record_v1 *completion,
                        void *buf,
                        size_t buf_size,
                        size_t *payload_length)
{
    if (iouringd_read_completion_payload(fd,
                                         completion,
                                         buf,
                                         buf_size,
                                         payload_length) != 0) {
        perror("read_completion_payload");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct iouringd_completion_record_v1 completion;
    struct iouringd_handshake_result_v1 handshake;
    struct iouringd_resource_id_record_v1 source_resource;
    struct iouringd_resource_id_record_v1 target_resource;
    struct iouringd_task_id_record_v1 read_task;
    struct iouringd_task_id_record_v1 write_task;
    char buffer[4096];
    size_t payload_length = 0u;
    int fd = -1;
    int source_fd = -1;
    int target_fd = -1;

    if (argc != 4) {
        fprintf(stderr,
                "usage: %s SOCKET_PATH SEALED_MEMFD_FD OVERLAY_TARGET_PATH\n",
                argv[0]);
        return 2;
    }

    source_fd = atoi(argv[2]);
    if (source_fd < 0) {
        fprintf(stderr, "invalid memfd fd: %s\n", argv[2]);
        return 2;
    }

    target_fd = open(argv[3], O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
    if (target_fd < 0) {
        perror("open target");
        return 1;
    }

    fd = iouringd_client_connect(argv[1]);
    if (fd < 0) {
        perror("connect");
        close(target_fd);
        return 1;
    }

    memset(&handshake, 0, sizeof(handshake));
    if (iouringd_client_handshake_fd(fd, &handshake) != 0) {
        perror("handshake");
        close(fd);
        close(target_fd);
        return 1;
    }

    if (handshake.capabilities.io_bytes_max < sizeof(buffer)) {
        fprintf(stderr,
                "daemon io_bytes_max=%u is smaller than the example buffer\n",
                handshake.capabilities.io_bytes_max);
        close(fd);
        close(target_fd);
        return 1;
    }

    memset(&source_resource, 0, sizeof(source_resource));
    if (iouringd_register_fd(fd, source_fd, &source_resource) != 0) {
        perror("register source memfd");
        close(fd);
        close(target_fd);
        return 1;
    }

    memset(&target_resource, 0, sizeof(target_resource));
    if (iouringd_register_fd(fd, target_fd, &target_resource) != 0) {
        perror("register target file");
        close(fd);
        close(target_fd);
        return 1;
    }

    memset(&read_task, 0, sizeof(read_task));
    if (iouringd_submit_file_read(fd,
                                  source_resource.resource_id,
                                  (uint32_t)sizeof(buffer),
                                  &read_task) != 0) {
        perror("submit_file_read");
        close(fd);
        close(target_fd);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(fd, &completion) != 0) {
        perror("wait source completion");
        close(fd);
        close(target_fd);
        return 1;
    }
    if (completion.task.task_id != read_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_FILE_READ ||
        completion.res < 0) {
        fprintf(stderr, "unexpected read completion: res=%d\n", completion.res);
        close(fd);
        close(target_fd);
        return 1;
    }

    if (read_payload(fd, &completion, buffer, sizeof(buffer), &payload_length) != 0) {
        close(fd);
        close(target_fd);
        return 1;
    }

    memset(&write_task, 0, sizeof(write_task));
    if (iouringd_submit_file_write(fd,
                                   target_resource.resource_id,
                                   buffer,
                                   (uint32_t)payload_length,
                                   &write_task) != 0) {
        perror("submit_file_write");
        close(fd);
        close(target_fd);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    if (iouringd_wait_completion(fd, &completion) != 0) {
        perror("wait target completion");
        close(fd);
        close(target_fd);
        return 1;
    }
    if (completion.task.task_id != write_task.task_id ||
        completion.task_kind.value != IOURINGD_TASK_KIND_FILE_WRITE ||
        completion.res < 0) {
        fprintf(stderr, "unexpected write completion: res=%d\n", completion.res);
        close(fd);
        close(target_fd);
        return 1;
    }

    close(fd);
    close(target_fd);
    return 0;
}
