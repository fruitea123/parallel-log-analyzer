# parallel-log-analyzer

CSC209 Category 1 parallel log analyzer in C.

The architecture remains intentionally simple:
- fixed pool of 3 worker processes
- anonymous pipes for parent-to-child tasks and child-to-parent results
- parent-side `select()` over the worker result pipes
- one-file-at-a-time dynamic scheduling once the initial 3 tasks are assigned

## Build

Use a POSIX environment such as Linux or WSL.

```sh
make
```

The binary is written to `bin/log_analyzer`.

## Run

The program requires at least 3 log-file paths:

```sh
./bin/log_analyzer <log1> <log2> <log3> [more_logs...]
```

Convenience demo:

```sh
make run
```

Example with the sample logs:

```sh
./bin/log_analyzer \
    sample_logs/app.log \
    sample_logs/api.log \
    sample_logs/db.log \
    sample_logs/mixed.log
```

## Output

Each completed file still prints its per-file worker result:

```text
worker 1 finished sample_logs/app.log: lines=4 errors=1 warnings=1
worker 2 failed missing.log: No such file or directory
```

After all files have been accounted for, the parent prints a final aggregate summary:

```text
summary: successful=4 failed=0 total_lines=15 total_errors=4 total_warnings=4
```

The summary totals count only successfully analyzed files. Failed files still contribute to the `failed=` count.

## Scheduling And IPC Notes

1. The parent creates two pipes per worker and then forks 3 children.
2. Each child closes every pipe end it does not own, then loops on `recv_task()`.
3. The parent closes its unused pipe ends immediately after forking and keeps only:
   - the task-pipe write ends
   - the result-pipe read ends
4. The parent initially sends one file to each worker.
5. The parent uses `select()` on the result pipes. When a result arrives:
   - it prints the per-file worker output,
   - updates the parent-side aggregate summary,
   - either sends the next file or sends `TASK_TERMINATE` if no files remain.
6. IPC helpers retry on `EINTR`, handle short reads/writes, and treat mid-message EOF as a protocol error.

## Unexpected Worker Exit Behavior

If a worker exits or closes its result pipe before the parent receives a full result message, the parent treats that as a fatal scheduler error. It reports:
- which worker failed
- which file was in flight
- any immediately available worker exit status

The parent then stops scheduling new work, closes remaining pipe ends, and reaps any children with `waitpid()`. This avoids silently losing an in-flight file.

## Testing Notes

The following checks were used during finalization:

```sh
make clean
make
./bin/log_analyzer sample_logs/app.log sample_logs/api.log sample_logs/db.log sample_logs/mixed.log
./bin/log_analyzer sample_logs/app.log sample_logs/api.log sample_logs/db.log missing.log
```

These cover:
- clean rebuild with warnings treated as errors
- successful processing of 4 files with the final aggregate summary
- continued processing when one file cannot be opened

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
