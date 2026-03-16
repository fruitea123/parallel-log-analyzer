# v0 Worker-Pool Scaffold for Parallel Log Analyzer

## Summary
- Build a single POSIX C program, `log_analyzer`, with a fixed worker pool of 3 child processes.
- CLI becomes `./bin/log_analyzer <log1> <log2> <log3> [more_logs...]`; reject fewer than 3 files.
- v0 scope stays minimal: each task analyzes one file for total lines, lines containing `ERROR`, and lines containing `WARNING`.
- Scheduling is lightweight but dynamic: parent seeds up to 3 workers, then uses a simple event loop to assign the next file to whichever worker finishes first.
- The parent may use `select()` to monitor multiple worker-result pipes, but the system remains a Category 1 multi-process design because all IPC is still done with anonymous pipes rather than sockets.

## Repo Structure
- `Makefile`: builds `bin/log_analyzer`, plus `run` and `clean`
- `include/protocol.h`: shared constants, message enums, IPC structs
- `include/ipc.h`: `read_full`, `write_full`, `send_task`, `recv_task`, `send_result`, `recv_result`
- `include/log_stats.h`: `analyze_log_file(const char *path, result_msg_t *out)`
- `include/worker.h`: `run_worker(int task_fd, int result_fd)`
- `src/main.c`: parent process setup, worker-state table, `select()`-based scheduler, result printing, cleanup
- `src/ipc.c`: fixed-size pipe I/O helpers
- `src/log_stats.c`: file analysis logic
- `src/worker.c`: worker receive/analyze/respond loop
- `sample_logs/`: at least 4 tiny log files so `make run` exercises rescheduling
- `README.md`: build/run steps and control-flow description

## IPC / Message Design
- Keep 2 dedicated pipes per worker:
  - parent -> worker: task messages
  - worker -> parent: result messages
- Shared constants:
```c
#define WORKER_COUNT 3
#define MAX_PATH_LEN 512
```
- Task message supports both work and shutdown:
```c
typedef enum {
    TASK_ANALYZE = 1,
    TASK_TERMINATE = 2
} task_type_t;

typedef struct {
    task_type_t type;
    char path[MAX_PATH_LEN];   // used only for TASK_ANALYZE
} task_msg_t;
```
- Result message is sent after each completed file:
```c
typedef struct {
    char path[MAX_PATH_LEN];
    uint64_t line_count;
    uint64_t error_count;
    uint64_t warning_count;
    int status;   // 0 = success, -1 = file/open/read failure
    int errnum;   // errno snapshot when status == -1
} result_msg_t;
```
- Protocol rules:
  - Parent sends exactly one `task_msg_t` at a time to a worker.
  - Worker exits only after receiving `TASK_TERMINATE`.
  - Worker sends exactly one `result_msg_t` for each `TASK_ANALYZE`.
  - File-processing failures are reported through `result_msg_t`; IPC/setup failures are fatal to the program.

## Updated Control Flow
- Parent startup:
  1. Validate `argc >= 4`.
  2. Create 3 worker slots, each with a task pipe, result pipe, `pid`, and state flags.
  3. `fork()` 3 children; each child keeps only its own read/write ends.
- Worker loop:
  1. Block on `recv_task`.
  2. If message is `TASK_TERMINATE`, close fds and exit `0`.
  3. If message is `TASK_ANALYZE`, analyze the file, send one result, then wait for the next task.
- Parent scheduler:
  1. Send the first 3 file paths, one per worker.
  2. Track `next_file_index`, `completed_files`, `busy` workers, and `terminated_workers`.
  3. Use `select()` on busy workers’ result-pipe read ends.
  4. When one worker returns a result:
     - receive and print that file’s counts,
     - if unassigned files remain, send that worker the next path,
     - otherwise send `TASK_TERMINATE` to that worker, mark it terminated, and increment `terminated_workers`.
  5. Continue the scheduling loop until all 3 workers have been sent `TASK_TERMINATE`, not merely until the last file result is received.
  6. Once `terminated_workers == WORKER_COUNT`, exit the scheduling loop and then `waitpid()` all 3 children to reap them cleanly.
- Output stays plain text, for example one line per completed file, optionally prefixed with worker index for clarity.

## Implementation Scaffold
- `src/main.c`
  - Define a small internal `worker_state_t` with `pid`, pipe fds, `busy`, and `terminated` flags.
  - Add helpers such as `spawn_worker`, `close_unused_parent_fds`, `dispatch_task`, and `send_terminate`.
  - Main event loop uses `select()` because the worker count is fixed and small.
- `src/worker.c`
  - Implement `run_worker(task_fd, result_fd)` as a `for (;;)` loop.
  - On `TASK_ANALYZE`, call `analyze_log_file`, populate `result_msg_t`, and send it.
  - On malformed/short IPC reads, exit non-zero.
- `src/log_stats.c`
  - Open with `fopen`.
  - Iterate with `getline`.
  - Count every line, and use `strstr(line, "ERROR")` / `strstr(line, "WARNING")`.
  - Fill `result_msg_t` directly so worker code stays thin.
- `src/ipc.c`
  - `read_full` / `write_full` loop until the full struct size is transferred or an error/EOF occurs.
  - Wrap those in typed helpers for tasks and results.
- `Makefile`
  - `CC = gcc`
  - `CFLAGS = -Wall -Wextra -Werror -std=c11 -g -D_POSIX_C_SOURCE=200809L -Iinclude`
  - `run` target should pass 4 sample logs to demonstrate that one worker receives a second task.

## Build / Run
- Build: `make`
- Demo: `make run`
- Manual run: `./bin/log_analyzer sample_logs/a.log sample_logs/b.log sample_logs/c.log sample_logs/d.log`

## Test Plan
- Build succeeds cleanly with warnings-as-errors.
- Running with 4 sample logs shows all files processed and the program exits after reaping all children.
- Running with exactly 3 logs still works and terminates each worker after its first result.
- A missing file produces an error result for that file but does not stop other workers from finishing.
- A line containing both `ERROR` and `WARNING` increments both counters.
- No hangs from open pipe ends: workers exit after terminate messages, parent exits after all `waitpid`s complete.

## Assumptions
- Target remains POSIX/Linux or WSL, not native Windows process APIs.
- Keep one executable with forked children; no separate parent/worker binaries.
- Dynamic scheduling is intentionally minimal: fixed worker pool, one outstanding task per worker, `select()` for readiness, no work stealing or batching.
- Output remains simple plain text; no sorting, aggregation, or fancy formatting in v0.
