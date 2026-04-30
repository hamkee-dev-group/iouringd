#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "iouringd/client.h"

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

int main(int argc, char **argv)
{
    char socket_path[108];
    pid_t child;
    int fd;
    int status;

    if (argc != 2) {
        return 2;
    }

    snprintf(socket_path,
             sizeof(socket_path),
             "/tmp/iouringd-graceful-shutdown-%ld.sock",
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

    if (kill(child, SIGTERM) != 0) {
        close(fd);
        waitpid(child, &status, 0);
        unlink(socket_path);
        return 1;
    }

    if (waitpid(child, &status, 0) != child) {
        close(fd);
        unlink(socket_path);
        return 1;
    }

    close(fd);

    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) {
        unlink(socket_path);
        return 1;
    }

    if (access(socket_path, F_OK) == 0 || errno != ENOENT) {
        unlink(socket_path);
        return 1;
    }

    return 0;
}
