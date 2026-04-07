# Parallel Log Analyzer

## 1. Project Overview

Parallel Log Analyzer is a Category 1 CSC209 mini-project: a multi-process application that uses anonymous pipes for interprocess communication. The program accepts at least three log-file paths on the command line, creates a fixed pool of three worker processes, and distributes file-analysis tasks from a single parent controller. Each worker analyzes one log file at a time and sends a structured result back to the parent. The parent prints each completed result as soon as it arrives and then prints a final aggregate summary after all files have been accounted for. The overall architecture is reflected in the protocol definitions in `include/protocol.h` (lines 6-26), the scheduler and parent control logic in `src/main.c` (lines 17-480), the worker loop in `src/worker.c` (lines 24-67), and the file-analysis logic in `src/log_stats.c` (lines 22-59).

The implementation is intentionally simple. The parent creates two pipes per worker, forks exactly three children, assigns one initial file to each worker, and then uses `select()` to wait for whichever worker finishes first. When a result arrives, the parent either assigns that worker another file or sends a termination message if no files remain. This design allows the program to process multiple files concurrently without introducing shared-memory synchronization. The worker-count constant appears in `include/protocol.h` (line 6), while worker creation and task dispatch are implemented in `spawn_workers` in `src/main.c` (lines 131-155), `seed_initial_tasks` in `src/main.c` (lines 269-283), and `run_scheduler` in `src/main.c` (lines 286-380).

The program input is a list of log-file paths:

```sh
./bin/log_analyzer <log1> <log2> <log3> [more_logs...]
```

Its output consists of one per-file status line for each completed task, followed by a final summary line. On success, a worker reports the total number of lines in the file, the number of lines containing the substring `"ERROR"`, and the number of lines containing the substring `"WARNING"`. On failure, the worker reports the pathname and the corresponding error message. The parent aggregates counts only for successful files, as implemented in `print_result` and `update_summary` in `src/main.c` (lines 198-230), and prints the final totals in `print_summary` in `src/main.c` (lines 232-242).

## 2. Build Instructions

The project is intended for a POSIX environment such as Linux or WSL. The build is controlled by the Makefile in `Makefile` (lines 1-24). Running `make` with no arguments compiles the program and produces the executable `bin/log_analyzer`, as specified by the default target and link rule in `Makefile` (lines 9-18). The compiler flags are:

```text
-Wall -Wextra -Werror -std=c11 -g -D_POSIX_C_SOURCE=200809L -Iinclude
```

The exact commands required to build and run the project are:

```sh
make
./bin/log_analyzer <log1> <log2> <log3> [more_logs...]
```

A clean rebuild can be performed with:

```sh
make clean
make
```

The repository also provides a convenience target:

```sh
make run
```

This target runs the executable on four included sample logs, as defined in `Makefile` (lines 20-21). A representative manual example is:

```sh
./bin/log_analyzer \
    sample_logs/app.log \
    sample_logs/api.log \
    sample_logs/db.log \
    sample_logs/mixed.log
```

The sample input files included in the repository are stored in `sample_logs/`. The run syntax and sample invocation are also documented in `README.md` (lines 21-43). The program requires at least three file paths; otherwise it prints a usage message and exits, as implemented in `main` in `src/main.c` (lines 422-425) and `print_usage` in `src/main.c` (lines 408-414).

## 3. Architecture Diagram

Figure 1 presents the final architecture of the program. The system consists of one parent controller process and three worker processes. For each worker, the parent creates one pipe for tasks and one pipe for results. The parent writes task messages to the task pipes and reads result messages from the result pipes, while each worker reads only from its own task pipe and writes only to its own result pipe. The parent does not communicate with workers through shared memory, and there are no worker-to-worker pipes. This arrangement is established in `create_pipes` in `src/main.c` (lines 76-90), `close_child_unused_fds` in `src/main.c` (lines 93-114), and `close_parent_unused_fds` in `src/main.c` (lines 116-129).

[Insert Figure 1: Architecture Diagram here]

*Figure 1. Final process architecture for Parallel Log Analyzer, showing the parent controller, three worker processes, the direction of each pipe, and the message types that flow over each connection.*

The figure should match the implementation exactly. The parent sends `TASK_ANALYZE` and `TASK_TERMINATE` messages to workers using `task_msg_t`, and workers send `result_msg_t` structures back to the parent. The parent monitors the three result-pipe read ends with `select()` in `run_scheduler` in `src/main.c` (lines 296-325). Log files themselves are ordinary file inputs opened by workers inside `analyze_log_file` in `src/log_stats.c` (lines 22-59); they are not part of the IPC diagram.

## 4. Communication Protocol

All IPC messages in this project are transmitted as fixed-width binary C structs over anonymous pipes. The helper functions `send_task`, `recv_task`, `send_result`, and `recv_result` in `src/ipc.c` (lines 55-82) write and read exactly one struct at a time. At the transport level, `write_full` and `read_full` in `src/ipc.c` (lines 6-52) continue until the requested byte count has been transferred, retry on `EINTR`, and report a protocol error if a pipe closes in the middle of a message. Because the program writes native struct layouts directly, the byte layout is defined by the current compiler and ABI rather than by an explicitly packed external wire format. In the current GCC/Linux build of this repository, `task_msg_t` occupies 516 bytes and `result_msg_t` occupies 544 bytes. These exact sizes should therefore be treated as implementation-specific rather than portable across arbitrary ABIs.

### 4.1 Analyze Task Message

**Sender and receiver.**  
Parent to worker.

**Encoding.**  
The analyze task uses `task_msg_t`, defined in `include/protocol.h` (lines 14-17):

```c
typedef struct {
    task_type_t type;
    char path[MAX_PATH_LEN];
} task_msg_t;
```

The parent initializes the struct in `dispatch_analyze_task` in `src/main.c` (lines 157-177). It zeroes the struct, sets `type = TASK_ANALYZE`, and copies a NUL-terminated path string into `path`.

**Semantics.**  
This message instructs the receiving worker to analyze the file named in `path` and later return exactly one `result_msg_t`. After sending the message successfully, the parent marks the worker as busy and records the assigned path in its internal worker state in `dispatch_analyze_task` in `src/main.c` (lines 174-175).

**How the receiver knows how many bytes to read.**  
The worker calls `recv_task`, which calls `read_full` with `sizeof(task_msg_t)` in `src/ipc.c` (lines 59-67). Since both parent and worker are compiled from the same program and use the same struct definition from `include/protocol.h`, the worker knows that one complete task message consists of exactly `sizeof(task_msg_t)` bytes.

**Error handling.**  
If the parent is given a path whose length is greater than or equal to `MAX_PATH_LEN`, it fails before sending and sets `errno = ENAMETOOLONG`, as implemented in `dispatch_analyze_task` in `src/main.c` (lines 159-164). If a pipe write fails, `send_task` returns an error and the scheduler aborts. At the lower level, `write_full` in `src/ipc.c` (lines 33-52) retries on `EINTR` and returns failure if the write cannot complete.

### 4.2 Terminate Task Message

**Sender and receiver.**  
Parent to worker.

**Encoding.**  
The terminate task also uses `task_msg_t`. In `send_terminate_task` in `src/main.c` (lines 179-196), the parent zeroes the struct and sets `type = TASK_TERMINATE`.

**Semantics.**  
This message instructs the receiving worker to stop accepting further work, close its pipe file descriptors, and exit successfully. The worker checks for this case in `run_worker` in `src/worker.c` (lines 40-42) and then jumps to cleanup.

**How the receiver knows how many bytes to read.**  
As with the analyze task, the worker receives the message through `recv_task` in `src/ipc.c` (lines 59-67), which reads exactly `sizeof(task_msg_t)` bytes.

**Error handling.**  
If sending the termination message fails, the parent treats that as a scheduler failure and exits through cleanup. After a successful send, the parent closes that worker's task-pipe write end in `send_terminate_task` in `src/main.c` (lines 188-194), ensuring that the pipe is not reused incorrectly.

### 4.3 Worker Result Message

**Sender and receiver.**  
Worker to parent.

**Encoding.**  
The result message uses `result_msg_t`, defined in `include/protocol.h` (lines 19-26):

```c
typedef struct {
    char path[MAX_PATH_LEN];
    uint64_t line_count;
    uint64_t error_count;
    uint64_t warning_count;
    int status;
    int errnum;
} result_msg_t;
```

Workers fill this structure in `analyze_log_file` in `src/log_stats.c` (lines 22-59) and send it with `send_result` in `src/ipc.c` (lines 70-71). The path field records the file name, the three counters record the analysis result, `status` is `0` on success and `-1` on failure, and `errnum` stores the relevant error number for failed operations.

**Semantics.**  
This message reports the completion of one assigned file. On success, the counts are valid and the parent prints a "finished" line. On failure, the worker still sends a structured result, but the parent prints a "failed" line using `strerror(result->errnum)`. The parent handles these two cases in `print_result` in `src/main.c` (lines 198-218), and updates aggregate totals in `update_summary` in `src/main.c` (lines 220-230).

**How the receiver knows how many bytes to read.**  
The parent receives worker results through `recv_result`, which calls `read_full` with `sizeof(result_msg_t)` in `src/ipc.c` (lines 74-82). Therefore the parent expects exactly one full `result_msg_t` per completed task.

**Error handling.**  
If a worker cannot open or process a particular file, it does not crash immediately. Instead, `fail_result` in `src/log_stats.c` (lines 13-20) constructs a failure message with zero counts, `status = -1`, and the current `errno`. If the worker later cannot write the result message to the pipe, `run_worker` in `src/worker.c` (lines 53-56) prints an error and exits failure. On the parent side, if `recv_result` returns EOF or a partial message, `run_scheduler` in `src/main.c` (lines 341-351) treats this as a fatal scheduler error, reports which worker stopped and which file was in flight, and terminates scheduling rather than silently discarding unfinished work.

### 4.4 Transport-Level Protocol Guarantees

At the transport level, this implementation makes two guarantees. First, `read_full` and `write_full` ensure that message transfers are not truncated by short reads or short writes. Second, message boundaries are preserved by convention because every receive operation requests the full size of exactly one struct. If the receiver encounters EOF before that many bytes have arrived, `read_full` sets `errno = EPROTO` and returns failure in `src/ipc.c` (lines 10-18). This behavior is important because the scheduler depends on receiving exactly one complete result for every file that was dispatched.

## 5. Concurrency Model

The concurrency model is based on a fixed pool of worker processes. After validating the command-line arguments, `main` initializes worker state, creates two anonymous pipes per worker, and forks exactly three children in `spawn_workers` in `src/main.c` (lines 131-155). Each child closes every pipe end it does not need and then enters the worker loop in `run_worker` in `src/worker.c` (lines 24-67). In that loop, the worker blocks in `recv_task`, handles one task, sends one result, and then waits for the next task. There is no separate ready message; a worker is effectively ready whenever it is blocked waiting on its task pipe.

The parent seeds the initial work by assigning the first three input files, one to each worker, in `seed_initial_tasks` in `src/main.c` (lines 269-283). This guarantees that all three workers can run concurrently as soon as the initial file-analysis tasks begin. The parent then enters `run_scheduler` in `src/main.c` (lines 286-380), where it builds an `fd_set` containing the result-pipe read ends of workers that are currently busy and not yet terminated. The parent calls `select()` on those descriptors in `src/main.c` (lines 320-322), so it waits only for readiness on worker result pipes and does not block on any one worker in particular.

When a result arrives, the parent reads one full `result_msg_t`, marks that worker idle, prints the per-file output, and updates the aggregate summary in `run_scheduler` in `src/main.c` (lines 341-358). If additional file paths remain, the parent immediately dispatches the next file to that same worker in `src/main.c` (lines 360-364). If no files remain, the parent sends `TASK_TERMINATE` to that worker and increments the count of terminated workers in `src/main.c` (lines 365-369). This means that work is scheduled dynamically: whichever worker finishes first receives the next available task.

After all files have been processed, the parent prints the final summary and reaps all remaining child processes with `waitpid` in `wait_for_workers` in `src/main.c` (lines 383-405). If a worker exits unexpectedly before sending a complete result, the parent reports the problem using `describe_worker_status` in `src/main.c` (lines 244-267), stops scheduling new work, closes any remaining pipe file descriptors, and still attempts to reap the remaining children in cleanup. This cleanup logic is implemented in `main` in `src/main.c` (lines 416-480). As a result, the concurrency model remains simple while still handling abrupt worker failure in a controlled way.

## 6. Error Handling and Robustness

This project explicitly handles several bad runtime behaviors that could otherwise cause crashes, hangs, or silent data loss.

### 6.1 Too Few Input Files

The program requires at least three input file paths because it is designed around a fixed pool of three workers. If fewer than three file paths are provided, `main` checks `argc < WORKER_COUNT + 1`, prints a usage message, and exits with failure rather than attempting a partial run. This behavior is implemented in `main` in `src/main.c` (lines 422-425) and `print_usage` in `src/main.c` (lines 408-414).

### 6.2 Failure to Open or Process a Log File

If a worker cannot open a file, or if a read or close operation fails during analysis, the program handles the error on a per-file basis instead of aborting the entire job immediately. `analyze_log_file` in `src/log_stats.c` (lines 22-59) initializes a result structure, attempts to open the file, scans it line by line, and on failure calls `fail_result` in `src/log_stats.c` (lines 13-20). The resulting `result_msg_t` contains zero counts, `status = -1`, and the relevant `errno` value. The worker then sends that structured failure result to the parent, allowing the parent to report the failed file while continuing to schedule and collect results for all other files. This behavior is visible in `run_worker` in `src/worker.c` (lines 50-56) and `print_result` in `src/main.c` (lines 198-218).

### 6.3 Interrupted System Calls and Short I/O

Pipe I/O can be interrupted by signals or may return short transfers. The IPC layer handles both cases explicitly. `read_full` and `write_full` in `src/ipc.c` (lines 6-52) loop until the full message length has been transferred. If `read` or `write` fails with `EINTR`, the helper retries rather than treating the interruption as fatal. This design ensures that fixed-width task and result messages are transferred atomically from the perspective of the higher-level protocol wrappers.

### 6.4 Pipe Closure, Broken Pipe, or Mid-Message EOF

If one process closes a pipe before a full message is transferred, the program treats that as a protocol failure. In `read_full` in `src/ipc.c` (lines 12-18), a clean EOF before any bytes have been read returns `0`, but EOF after only part of a message has arrived sets `errno = EPROTO` and returns failure. Similarly, `main` ignores `SIGPIPE` in `src/main.c` (lines 427-430), so a write to a closed pipe does not terminate the parent process unexpectedly. Instead, the write fails in a controlled way and the scheduler can handle the error.

### 6.5 Unexpected Worker Exit Before Sending a Result

If a worker exits or closes its result pipe before the parent receives a full `result_msg_t`, the parent treats that as a fatal scheduler error because an in-flight file result may otherwise be lost. In `run_scheduler` in `src/main.c` (lines 341-351), the parent reports which worker stopped and which file was assigned to it. It then calls `describe_worker_status` in `src/main.c` (lines 244-267), which uses `waitpid(..., WNOHANG)` to report any immediately available exit status. After that, the parent leaves the scheduler, closes remaining file descriptors, and reaps children in cleanup. This behavior prevents the program from silently producing an incomplete summary.

### 6.6 Failures of `pipe`, `fork`, `select`, and `waitpid`

All major system calls in the parent are checked. Pipe-creation errors are handled in `create_pipes` in `src/main.c` (lines 76-90) and the call site in `main` in `src/main.c` (lines 434-437). Fork failures are handled in `spawn_workers` in `src/main.c` (lines 131-155) and the call site in `main` in `src/main.c` (lines 439-441). `select()` failures are handled in `run_scheduler` in `src/main.c` (lines 320-325), and `waitpid()` failures are handled in `wait_for_workers` in `src/main.c` (lines 394-395) and `main` in `src/main.c` (lines 455-458 and 470-477). In each case, the program prints an error and enters cleanup rather than continuing in an inconsistent state.

### 6.7 Resource Cleanup and Zombie Prevention

The implementation also addresses resource leaks. Unused pipe ends are closed immediately after `fork` in `close_child_unused_fds` and `close_parent_unused_fds` in `src/main.c` (lines 93-129), and `close_all_worker_fds` in `src/main.c` (lines 65-74) is called again during cleanup to close any remaining tracked descriptors. Child processes are reaped by `wait_for_workers` in `src/main.c` (lines 383-405), which helps prevent zombie processes. The worker also closes its own task and result file descriptors before exiting in `run_worker` in `src/worker.c` (lines 59-66).

## 7. Team Contributions

This section must be completed manually so that it reflects the actual work performed by the team. The repository does not provide enough evidence to assign contributions accurately, so no contributions are inferred here.

### Template for 1 Teammate

`[Name] contributed to the design, implementation, testing, and documentation of the parallel log analyzer. Their main responsibilities included [specific files, modules, features, tests, or writing tasks].`

### Template for 2 Teammates

`[Name 1] focused on [specific files, modules, features, tests, or writing tasks]. Their work included [brief concrete details].`

`[Name 2] focused on [specific files, modules, features, tests, or writing tasks]. Their work included [brief concrete details].`

### Template for 3 Teammates

`[Name 1] focused on [specific files, modules, features, tests, or writing tasks].`

`[Name 2] focused on [specific files, modules, features, tests, or writing tasks].`

`[Name 3] focused on [specific files, modules, features, tests, or writing tasks].`
