# parallel-log-analyzer

Minimal v0 scaffold for a CSC209 Category 1 parallel log analyzer in C.

This version keeps the design intentionally small:
- fixed worker pool of 3 child processes
- parent/worker communication over anonymous pipes
- lightweight dynamic scheduling for 3 or more log files
- per-file counts for total lines, `ERROR`, and `WARNING`

`select()` is used only by the parent to monitor multiple worker-to-parent pipe read ends. This is still a Category 1 multi-process, pipe-based design, not a socket-based Category 2 design.

## Repository Layout

```text
include/
  ipc.h
  log_stats.h
  protocol.h
  worker.h
src/
  ipc.c
  log_stats.c
  main.c
  worker.c
sample_logs/
  api.log
  app.log
  db.log
  mixed.log
Makefile
```

## Build

Use a POSIX environment such as Linux or WSL.

```sh
make
```

## Run

Demo run with four sample logs:

```sh
make run
```

Manual run:

```sh
./bin/log_analyzer sample_logs/app.log sample_logs/api.log sample_logs/db.log sample_logs/mixed.log
```

The program requires at least 3 log-file paths.

## Control Flow

1. The parent creates two pipes per worker and forks 3 children.
2. Each worker loops forever: receive a task, exit on `TASK_TERMINATE`, otherwise analyze one file and send back one result.
3. The parent initially sends one file to each worker.
4. The parent uses `select()` on the result-pipe read ends. When a worker finishes:
   - the parent reads and prints the result,
   - if unassigned files remain, it sends that worker the next file,
   - otherwise it sends `TASK_TERMINATE` to that worker and marks it terminated.
5. The parent tracks `terminated_workers` explicitly and does not leave the scheduling loop until all 3 workers have been sent `TASK_TERMINATE`.
6. After the scheduling loop ends, the parent calls `waitpid()` for all children and reports abnormal exits.

## Sample Output Shape

Successful results print one line per file:

```text
worker 1 finished sample_logs/app.log: lines=4 errors=1 warnings=1
```

File errors are reported without stopping the rest of the worker pool:

```text
worker 2 failed missing.log: No such file or directory
```
