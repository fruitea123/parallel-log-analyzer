#ifndef IPC_H
#define IPC_H

#include <stddef.h>
#include <sys/types.h>

#include "protocol.h"

ssize_t read_full(int fd, void *buf, size_t count);
int write_full(int fd, const void *buf, size_t count);

int send_task(int fd, const task_msg_t *msg);
int recv_task(int fd, task_msg_t *msg);

int send_result(int fd, const result_msg_t *msg);
int recv_result(int fd, result_msg_t *msg);

#endif
