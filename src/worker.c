#include "worker.h"

#include <stdlib.h>
#include <unistd.h>

#include "ipc.h"
#include "log_stats.h"
#include "protocol.h"

static void close_fd_if_open(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

int run_worker(int task_fd, int result_fd) {
    int exit_code = EXIT_FAILURE;

    for (;;) {
        task_msg_t task;
        int recv_status = recv_task(task_fd, &task);

        if (recv_status != 1) {
            goto cleanup;
        }

        if (task.type == TASK_TERMINATE) {
            exit_code = EXIT_SUCCESS;
            goto cleanup;
        }

        if (task.type != TASK_ANALYZE) {
            goto cleanup;
        }

        result_msg_t result;
        analyze_log_file(task.path, &result);

        if (send_result(result_fd, &result) < 0) {
            goto cleanup;
        }
    }

cleanup:
    close_fd_if_open(task_fd);
    close_fd_if_open(result_fd);
    return exit_code;
}
