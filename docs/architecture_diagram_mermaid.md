# Architecture Diagram Draft

## Items That Must Appear in the Final Figure

- One parent controller process.
- Three worker processes labeled clearly as separate children.
- One parent-to-worker task pipe for each worker.
- One worker-to-parent result pipe for each worker.
- Arrow directions on every pipe.
- Labels on task pipes showing `task_msg_t` with `TASK_ANALYZE` and `TASK_TERMINATE`.
- Labels on result pipes showing `result_msg_t` with `path`, counts, `status`, and `errnum`.
- A note that the parent uses `select()` on the result-pipe read ends.
- External log files shown, if desired, as regular file inputs to workers rather than as IPC channels.
- No worker-to-worker pipes.

## Mermaid Diagram

```mermaid
flowchart LR
    P["Parent Controller<br/>creates pipes and forks workers<br/>dispatches tasks<br/>select() on result pipes<br/>prints results and summary"]

    W1["Worker 1<br/>run_worker()"]
    W2["Worker 2<br/>run_worker()"]
    W3["Worker 3<br/>run_worker()"]

    F["Log files on disk<br/>sample_logs/*.log"]

    P -- "task pipe<br/>task_msg_t<br/>TASK_ANALYZE / TASK_TERMINATE" --> W1
    P -- "task pipe<br/>task_msg_t<br/>TASK_ANALYZE / TASK_TERMINATE" --> W2
    P -- "task pipe<br/>task_msg_t<br/>TASK_ANALYZE / TASK_TERMINATE" --> W3

    W1 -- "result pipe<br/>result_msg_t<br/>path, line_count, error_count,<br/>warning_count, status, errnum" --> P
    W2 -- "result pipe<br/>result_msg_t<br/>path, line_count, error_count,<br/>warning_count, status, errnum" --> P
    W3 -- "result pipe<br/>result_msg_t<br/>path, line_count, error_count,<br/>warning_count, status, errnum" --> P

    F -. "opened and analyzed as regular files" .-> W1
    F -. "opened and analyzed as regular files" .-> W2
    F -. "opened and analyzed as regular files" .-> W3
```
