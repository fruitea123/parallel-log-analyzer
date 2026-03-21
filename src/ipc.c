#include "ipc.h"

#include <errno.h>
#include <unistd.h>

ssize_t read_full(int fd, void *buf, size_t count) {
    size_t total = 0;
    char *cursor = buf;

    while (total < count) {
        ssize_t bytes_read = read(fd, cursor + total, count - total);
        if (bytes_read == 0) {
            if (total == 0) {
                return 0;
            }
            errno = EPROTO;
            return -1;
        }
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)bytes_read;
    }

    return (ssize_t)total;
}

int write_full(int fd, const void *buf, size_t count) {
    size_t total = 0;
    const char *cursor = buf;

    while (total < count) {
        ssize_t bytes_written = write(fd, cursor + total, count - total);
        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)bytes_written;
    }

    return 0;
}

int send_task(int fd, const task_msg_t *msg) {
    return write_full(fd, msg, sizeof(*msg));
}

int recv_task(int fd, task_msg_t *msg) {
    ssize_t bytes_read = read_full(fd, msg, sizeof(*msg));
    if (bytes_read == 0) {
        return 0;
    }
    if (bytes_read < 0) {
        return -1;
    }
    return 1;
}

int send_result(int fd, const result_msg_t *msg) {
    return write_full(fd, msg, sizeof(*msg));
}

int recv_result(int fd, result_msg_t *msg) {
    ssize_t bytes_read = read_full(fd, msg, sizeof(*msg));
    if (bytes_read == 0) {
        return 0;
    }
    if (bytes_read < 0) {
        return -1;
    }
    return 1;
}
