#include "worker.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ipc.h"
#include "log_stats.h"
#include "protocol.h"

static int close_fd_if_open(int fd, const char *what) {
    if (fd >= 0) {
        if (close(fd) < 0) {
            fprintf(stderr, "%s: %s\n", what, strerror(errno));
            return -1;
        }
    }

    return 0;
}

int run_worker(int task_fd, int result_fd) {
    int exit_code = EXIT_FAILURE;

    for (;;) {
        task_msg_t task;
        int recv_status = recv_task(task_fd, &task);

        if (recv_status == 0) {
            fprintf(stderr, "worker: task pipe closed unexpectedly\n");
            goto cleanup;
        }
        if (recv_status < 0) {
            fprintf(stderr, "worker: failed to receive task: %s\n", strerror(errno));
            goto cleanup;
        }

        if (task.type == TASK_TERMINATE) {
            exit_code = EXIT_SUCCESS;
            goto cleanup;
        }

        if (task.type != TASK_ANALYZE) {
            fprintf(stderr, "worker: received unknown task type %d\n", task.type);
            goto cleanup;
        }

        result_msg_t result;
        analyze_log_file(task.path, &result);

        if (send_result(result_fd, &result) < 0) {
            fprintf(stderr, "worker: failed to send result for %s: %s\n", task.path, strerror(errno));
            goto cleanup;
        }
    }

cleanup:
    if (close_fd_if_open(task_fd, "worker close task fd failed") < 0) {
        exit_code = EXIT_FAILURE;
    }
    if (close_fd_if_open(result_fd, "worker close result fd failed") < 0) {
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
