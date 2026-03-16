# v0 Multi-Process Log Analyzer Scaffold

## Summary
- Build one POSIX C executable, `log_analyzer`, that accepts exactly 3 log paths, forks 3 children, sends one path to each child over a dedicated parent-to-worker pipe, receives one result over a dedicated worker-to-parent pipe, prints counts, and `waitpid`s all children.
- Keep v0 intentionally fixed-shape: 3 workers, 3 CLI args, one file per worker, one result per worker, no dynamic scheduling, no extra keywords, no advanced parsing.

## Public Interface / Types
- CLI: `./bin/log_analyzer <log1> <log2> <log3>`
- Shared constants in `include/common.h`: `WORKER_COUNT = 3`, `MAX_PATH_LEN = 512`
- Shared IPC message types:
```c
typedef struct {
    char path[MAX_PATH_LEN];
} task_msg_t;

typedef struct {
    char path[MAX_PATH_LEN];
    uint64_t line_count;
    uint64_t error_count;
    uint64_t warning_count;
    int status;   // 0 = success, -1 = failure
    int errnum;   // copied from errno when status == -1
} result_msg_t;
```
- IPC helpers in `include/ipc.h`: `read_full`, `write_full`, `send_task`, `recv_task`, `send_result`, `recv_result`
- Log analysis API in `include/log_stats.h`: `int analyze_log_file(const char *path, result_msg_t *out);`

## Repo Structure
- `Makefile`: build `bin/log_analyzer`, plus `run` and `clean`
- `include/common.h`: constants, shared structs, common includes
- `include/ipc.h`: pipe helper prototypes
- `include/log_stats.h`: log analysis prototype
- `src/main.c`: parent orchestration, fork/pipe lifecycle, result printing, waiting
- `src/worker.c`: child entrypoint `run_worker(task_fd, result_fd)`
- `src/ipc.c`: exact-size pipe read/write wrappers and message send/receive helpers
- `src/log_stats.c`: open file, iterate lines, count total / `ERROR` / `WARNING`
- `sample_logs/app.log`, `sample_logs/api.log`, `sample_logs/db.log`: tiny demo inputs
- `README.md`: build/run steps and control-flow summary

## IPC Protocol
- Topology: 2 pipes per worker
- Parent -> worker pipe carries exactly one `task_msg_t`
- Worker -> parent pipe carries exactly one `result_msg_t`
- Serialization: raw fixed-size binary structs, always sent with `write_full` and read with `read_full`
- Rules:
  - Parent validates `strlen(path) < MAX_PATH_LEN` before sending
  - Worker copies the input path into the result so the parent can identify responses directly
  - `status = 0` on success, `-1` on failure; on failure, counts stay `0` and `errnum` is set
- Reasoning: one fixed-size message each way keeps v0 simple while still exercising real pipe IPC

## Implementation Scaffold
- `main.c`
  1. Validate `argc == 4`; otherwise print usage and exit non-zero.
  2. For each worker index `0..2`, create `task_pipe[i]` and `result_pipe[i]`, then `fork()`.
  3. In each child:
     - Close every pipe end not needed by that child.
     - Keep only `task_pipe[i][0]` and `result_pipe[i][1]`.
     - Call `run_worker(task_pipe[i][0], result_pipe[i][1])`, then `_exit(status)`.
  4. In the parent:
     - After each fork, close `task_pipe[i][0]` and `result_pipe[i][1]`.
     - After all forks, send one `task_msg_t` per worker using the 3 CLI paths, then close each task write end.
     - Receive one `result_msg_t` per worker from each result pipe, print either counts or an error line, then close each result read end.
     - `waitpid` all 3 children and report abnormal exits.
- `worker.c`
  - `run_worker` reads one `task_msg_t`; on EOF or short read, exits non-zero.
  - It calls `analyze_log_file(path, &result)` and sends one `result_msg_t`.
  - It closes its pipe fds before exit.
- `log_stats.c`
  - Open with `fopen`.
  - Use `getline` in a loop so long log lines are still counted as one line.
  - Increment `line_count` for every line read.
  - Increment `error_count` if `strstr(line, "ERROR") != NULL`.
  - Increment `warning_count` if `strstr(line, "WARNING") != NULL`.
- `Makefile`
  - `CC = gcc`
  - `CFLAGS = -Wall -Wextra -Werror -std=c11 -g -D_POSIX_C_SOURCE=200809L -Iinclude`
  - Targets: `all`, `run`, `clean`

## Build / Run / Control Flow
- Build: `make`
- Demo run: `make run` or `./bin/log_analyzer sample_logs/app.log sample_logs/api.log sample_logs/db.log`
- Control flow: parent creates all workers first, distributes one path to each child, children analyze independently, parent gathers one result from each pipe, prints a simple summary, then reaps every child with `waitpid`

## Test Plan
- `make` succeeds with warnings treated as errors
- `make run` prints 3 result lines with correct totals for the checked-in sample logs
- A line containing both `ERROR` and `WARNING` increments both counters
- A missing file path returns `status = -1` from that worker and does not crash the parent
- Unused pipe ends are closed correctly so the program does not hang
- All 3 children are reaped after normal execution

## Assumptions
- Target environment is POSIX/Linux or WSL, not native Windows process APIs
- v0 is a single executable with forked children, not separate parent/worker binaries
- Worker count stays fixed at 3 for this step
- Output stays plain text as long as each file’s counts are clearly printed
