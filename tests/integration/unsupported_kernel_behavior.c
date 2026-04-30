#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static ssize_t read_all(int fd, char *buf, size_t capacity)
{
    size_t offset = 0u;

    while (offset + 1u < capacity) {
        ssize_t rc = read(fd, buf + offset, capacity - offset - 1u);

        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            break;
        }
        offset += (size_t)rc;
    }

    buf[offset] = '\0';
    return (ssize_t)offset;
}

int main(int argc, char **argv)
{
    char socket_path[108];
    char stderr_buf[1024];
    int stderr_pipe[2];
    int status;
    pid_t child;

    if (argc != 2) {
        return 2;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-unsupported-kernel-%ld.sock",
             (long)getpid());
    unlink(socket_path);

    if (pipe(stderr_pipe) != 0) {
        return 1;
    }

    child = fork();
    if (child < 0) {
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return 1;
    }

    if (child == 0) {
        close(stderr_pipe[0]);
        if (dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        close(stderr_pipe[1]);
        setenv("IOURINGD_TEST_FAULT", "setup_enosys", 1);
        execl(argv[1], argv[1], socket_path, (char *)NULL);
        _exit(127);
    }

    close(stderr_pipe[1]);
    memset(stderr_buf, 0, sizeof(stderr_buf));

    if (waitpid(child, &status, 0) != child) {
        close(stderr_pipe[0]);
        unlink(socket_path);
        return 1;
    }

    if (read_all(stderr_pipe[0], stderr_buf, sizeof(stderr_buf)) < 0) {
        close(stderr_pipe[0]);
        unlink(socket_path);
        return 1;
    }

    close(stderr_pipe[0]);
    unlink(socket_path);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 1) {
        return 1;
    }

    if (strstr(stderr_buf, "io_uring setup is unavailable on this kernel") == NULL ||
        strstr(stderr_buf, "ENOSYS") == NULL) {
        return 1;
    }

    return 0;
}
