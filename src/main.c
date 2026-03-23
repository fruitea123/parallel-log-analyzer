#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ipc.h"
#include "protocol.h"
#include "worker.h"

typedef struct {
    pid_t pid;
    int task_pipe[2];
    int result_pipe[2];
    bool busy;
    bool terminated;
    const char *assigned_path;
} worker_state_t;

typedef struct {
    size_t successful_files;
    size_t failed_files;
    uint64_t total_lines;
    uint64_t total_errors;
    uint64_t total_warnings;
} aggregate_summary_t;

static int close_fd_if_open(int *fd, const char *what) {
    if (*fd >= 0) {
        if (close(*fd) < 0) {
            int saved_errno = errno;

            fprintf(stderr, "%s: close failed: %s\n", what, strerror(saved_errno));
            *fd = -1;
            errno = saved_errno;
            return -1;
        }
        *fd = -1;
    }

    return 0;
}

static void init_workers(worker_state_t workers[WORKER_COUNT]) {
    size_t i;

    for (i = 0; i < WORKER_COUNT; ++i) {
        workers[i].pid = -1;
        workers[i].task_pipe[0] = -1;
        workers[i].task_pipe[1] = -1;
        workers[i].result_pipe[0] = -1;
        workers[i].result_pipe[1] = -1;
        workers[i].busy = false;
        workers[i].terminated = false;
        workers[i].assigned_path = NULL;
    }
}

static void close_all_worker_fds(worker_state_t workers[WORKER_COUNT]) {
    size_t i;

    for (i = 0; i < WORKER_COUNT; ++i) {
        (void)close_fd_if_open(&workers[i].task_pipe[0], "close task pipe read end");
        (void)close_fd_if_open(&workers[i].task_pipe[1], "close task pipe write end");
        (void)close_fd_if_open(&workers[i].result_pipe[0], "close result pipe read end");
        (void)close_fd_if_open(&workers[i].result_pipe[1], "close result pipe write end");
    }
}

static int create_pipes(worker_state_t workers[WORKER_COUNT]) {
    size_t i;

    for (i = 0; i < WORKER_COUNT; ++i) {
        if (pipe(workers[i].task_pipe) < 0) {
            close_all_worker_fds(workers);
            return -1;
        }
        if (pipe(workers[i].result_pipe) < 0) {
            close_all_worker_fds(workers);
            return -1;
        }
    }

    return 0;
}

static int close_child_unused_fds(worker_state_t workers[WORKER_COUNT], size_t self_index) {
    size_t i;

    for (i = 0; i < WORKER_COUNT; ++i) {
        if (close_fd_if_open(&workers[i].task_pipe[1], "child close task pipe write end") < 0) {
            return -1;
        }
        if (close_fd_if_open(&workers[i].result_pipe[0], "child close result pipe read end") < 0) {
            return -1;
        }
        if (i != self_index) {
            if (close_fd_if_open(&workers[i].task_pipe[0], "child close foreign task pipe read end") < 0) {
                return -1;
            }
            if (close_fd_if_open(&workers[i].result_pipe[1], "child close foreign result pipe write end") < 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int close_parent_unused_fds(worker_state_t workers[WORKER_COUNT]) {
    size_t i;

    for (i = 0; i < WORKER_COUNT; ++i) {
        if (close_fd_if_open(&workers[i].task_pipe[0], "parent close task pipe read end") < 0) {
            return -1;
        }
        if (close_fd_if_open(&workers[i].result_pipe[1], "parent close result pipe write end") < 0) {
            return -1;
        }
    }

    return 0;
}

static int spawn_workers(worker_state_t workers[WORKER_COUNT]) {
    size_t i;

    for (i = 0; i < WORKER_COUNT; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            return -1;
        }

        if (pid == 0) {
            int child_status;

            if (close_child_unused_fds(workers, i) < 0) {
                _exit(EXIT_FAILURE);
            }

            child_status = run_worker(workers[i].task_pipe[0], workers[i].result_pipe[1]);
            _exit(child_status);
        }

        workers[i].pid = pid;
    }

    return 0;
}

static int dispatch_analyze_task(worker_state_t *worker, const char *path) {
    task_msg_t task;
    size_t path_length = strlen(path);

    if (path_length >= MAX_PATH_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memset(&task, 0, sizeof(task));
    task.type = TASK_ANALYZE;
    memcpy(task.path, path, path_length + 1);

    if (send_task(worker->task_pipe[1], &task) < 0) {
        return -1;
    }

    worker->busy = true;
    worker->assigned_path = path;
    return 0;
}

static int send_terminate_task(worker_state_t *worker) {
    task_msg_t task;

    memset(&task, 0, sizeof(task));
    task.type = TASK_TERMINATE;

    if (send_task(worker->task_pipe[1], &task) < 0) {
        return -1;
    }
    if (close_fd_if_open(&worker->task_pipe[1], "parent close terminated worker task pipe write end") < 0) {
        return -1;
    }

    worker->busy = false;
    worker->terminated = true;
    worker->assigned_path = NULL;
    return 0;
}

static void print_result(size_t worker_index, const result_msg_t *result) {
    if (result->status == 0) {
        printf(
            "worker %zu finished %s: lines=%" PRIu64 " errors=%" PRIu64 " warnings=%" PRIu64 "\n",
            worker_index + 1,
            result->path,
            result->line_count,
            result->error_count,
            result->warning_count
        );
        return;
    }

    fprintf(
        stderr,
        "worker %zu failed %s: %s\n",
        worker_index + 1,
        result->path,
        strerror(result->errnum)
    );
}

static void update_summary(aggregate_summary_t *summary, const result_msg_t *result) {
    if (result->status == 0) {
        summary->successful_files++;
        summary->total_lines += result->line_count;
        summary->total_errors += result->error_count;
        summary->total_warnings += result->warning_count;
        return;
    }

    summary->failed_files++;
}

static void print_summary(const aggregate_summary_t *summary) {
    printf(
        "summary: successful=%zu failed=%zu total_lines=%" PRIu64 " total_errors=%" PRIu64
        " total_warnings=%" PRIu64 "\n",
        summary->successful_files,
        summary->failed_files,
        summary->total_lines,
        summary->total_errors,
        summary->total_warnings
    );
}

static void describe_worker_status(size_t worker_index, worker_state_t *worker) {
    int status;
    pid_t wait_result;

    wait_result = waitpid(worker->pid, &status, WNOHANG);
    if (wait_result == 0) {
        fprintf(stderr, "worker %zu is still running but its result pipe closed unexpectedly\n", worker_index + 1);
        return;
    }
    if (wait_result < 0) {
        fprintf(stderr, "worker %zu status check failed: %s\n", worker_index + 1, strerror(errno));
        return;
    }

    if (WIFEXITED(status)) {
        fprintf(stderr, "worker %zu exited with status %d\n", worker_index + 1, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "worker %zu was terminated by signal %d\n", worker_index + 1, WTERMSIG(status));
    } else {
        fprintf(stderr, "worker %zu changed state unexpectedly\n", worker_index + 1);
    }

    worker->pid = -1;
}

static int seed_initial_tasks(
    worker_state_t workers[WORKER_COUNT],
    char *argv[],
    int *next_file_index
) {
    size_t i;

    for (i = 0; i < WORKER_COUNT; ++i) {
        if (dispatch_analyze_task(&workers[i], argv[(int)i + 1]) < 0) {
            return -1;
        }
    }

    *next_file_index = WORKER_COUNT + 1;
    return 0;
}

static int run_scheduler(worker_state_t workers[WORKER_COUNT], char *argv[], int argc) {
    aggregate_summary_t summary = {0};
    size_t completed_files = 0;
    size_t terminated_workers = 0;
    int next_file_index = 0;

    if (seed_initial_tasks(workers, argv, &next_file_index) < 0) {
        return -1;
    }

    while (terminated_workers < WORKER_COUNT) {
        fd_set readfds;
        int max_fd = -1;
        int ready_count;
        size_t i;

        FD_ZERO(&readfds);

        for (i = 0; i < WORKER_COUNT; ++i) {
            if (!workers[i].busy || workers[i].terminated) {
                continue;
            }

            FD_SET(workers[i].result_pipe[0], &readfds);
            if (workers[i].result_pipe[0] > max_fd) {
                max_fd = workers[i].result_pipe[0];
            }
        }

        if (max_fd < 0) {
            errno = EPROTO;
            return -1;
        }

        do {
            ready_count = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        } while (ready_count < 0 && errno == EINTR);

        if (ready_count < 0) {
            return -1;
        }

        for (i = 0; i < WORKER_COUNT && ready_count > 0; ++i) {
            int recv_status;
            result_msg_t result;

            if (!workers[i].busy || workers[i].terminated) {
                continue;
            }

            if (!FD_ISSET(workers[i].result_pipe[0], &readfds)) {
                continue;
            }

            ready_count--;
            recv_status = recv_result(workers[i].result_pipe[0], &result);
            if (recv_status != 1) {
                fprintf(
                    stderr,
                    "worker %zu stopped before sending a complete result for %s\n",
                    i + 1,
                    workers[i].assigned_path != NULL ? workers[i].assigned_path : "<no assigned file>"
                );
                describe_worker_status(i, &workers[i]);
                errno = recv_status == 0 ? EPROTO : errno;
                return -1;
            }

            workers[i].busy = false;
            workers[i].assigned_path = NULL;
            completed_files++;
            print_result(i, &result);
            update_summary(&summary, &result);

            if (next_file_index < argc) {
                if (dispatch_analyze_task(&workers[i], argv[next_file_index]) < 0) {
                    return -1;
                }
                next_file_index++;
            } else {
                if (send_terminate_task(&workers[i]) < 0) {
                    return -1;
                }
                terminated_workers++;
            }
        }
    }

    if (completed_files != (size_t)(argc - 1)) {
        errno = EPROTO;
        return -1;
    }

    print_summary(&summary);
    return 0;
}

static int wait_for_workers(worker_state_t workers[WORKER_COUNT]) {
    int had_child_failure = 0;
    size_t i;

    for (i = 0; i < WORKER_COUNT; ++i) {
        int status;

        if (workers[i].pid <= 0) {
            continue;
        }

        if (waitpid(workers[i].pid, &status, 0) < 0) {
            return -1;
        }
        workers[i].pid = -1;

        if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
            fprintf(stderr, "worker %zu exited abnormally\n", i + 1);
            had_child_failure = 1;
        }
    }

    return had_child_failure;
}

static void print_usage(const char *program_name) {
    fprintf(
        stderr,
        "usage: %s <log1> <log2> <log3> [more_logs...]\n",
        program_name
    );
}

int main(int argc, char *argv[]) {
    worker_state_t workers[WORKER_COUNT];
    int exit_code = EXIT_FAILURE;
    int wait_status = 0;
    bool workers_reaped = false;

    if (argc < WORKER_COUNT + 1) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return EXIT_FAILURE;
    }

    init_workers(workers);

    if (create_pipes(workers) < 0) {
        perror("pipe");
        goto cleanup;
    }

    if (spawn_workers(workers) < 0) {
        perror("fork");
        goto cleanup;
    }

    if (close_parent_unused_fds(workers) < 0) {
        goto cleanup;
    }

    if (run_scheduler(workers, argv, argc) < 0) {
        perror("scheduler");
        goto cleanup;
    }

    close_all_worker_fds(workers);

    wait_status = wait_for_workers(workers);
    if (wait_status < 0) {
        perror("waitpid");
        goto cleanup;
    }
    workers_reaped = true;
    if (wait_status > 0) {
        goto cleanup;
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    close_all_worker_fds(workers);

    if (!workers_reaped) {
        wait_status = wait_for_workers(workers);
        if (wait_status < 0) {
            perror("waitpid");
            exit_code = EXIT_FAILURE;
        } else if (wait_status > 0) {
            exit_code = EXIT_FAILURE;
        }
    }

    return exit_code;
}
