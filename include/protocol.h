#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define WORKER_COUNT 3
#define MAX_PATH_LEN 512

typedef enum {
    TASK_ANALYZE = 1,
    TASK_TERMINATE = 2
} task_type_t;

typedef struct {
    task_type_t type;
    char path[MAX_PATH_LEN];
} task_msg_t;

typedef struct {
    char path[MAX_PATH_LEN];
    uint64_t line_count;
    uint64_t error_count;
    uint64_t warning_count;
    int status;
    int errnum;
} result_msg_t;

#endif
