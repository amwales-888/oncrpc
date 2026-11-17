# 07_errors: credentials and failures

| File       | What it is                                                    |
|------------|---------------------------------------------------------------|
| `secure.x` | `WHOAMI`, `ADMIN_ONLY` and `NO_REPLY`                          |
| `server.c` | reads credentials, rejects non-root callers, drops one request |
| `client.c` | six steps, one after another, each showing a kind of outcome   |

```sh
build/linux/release/examples/07_errors/server &
build/linux/release/examples/07_errors/client
# client: 1. WHOAMI with AUTH_NONE           success                        (as expected)
# client:    server sees: anonymous (AUTH_NONE)
# client: 2. WHOAMI with AUTH_SYS            success                        (as expected)
# client:    server sees: uid 1000, gid 1000 on example-host (AUTH_SYS)
# client: 3. ADMIN_ONLY as uid 1000          authentication error           (as expected)
# client:    reason: auth_stat 5 (too weak)
# client: 4. procedure 99                    procedure unavailable          (as expected)
# client: 5. NO_REPLY, 500 ms timeout        timed out                      (as expected)
# client: 6. version 2                       program version mismatch       (as expected)
# client:    server supports versions 1 to 1
```

Things to notice:

- **Credentials.** `oncRPCClientSetAuthSys()` (or
  `oncRPCClientSetAuthSysDefault()`) attaches AUTH_SYS credentials to every
  later call. The server reads them in `req->authSys`, which is NULL for
  AUTH_NONE.
- **Rejecting a caller.** `oncRPCSvcErrAuth(req, why)` turns the call away.
  The client gets `ONCRPC_ERR_AUTHERROR`, and the reason is in
  `oncRPCClientLastError(client)->why`.
- **Automatic errors.** You write no code for unknown procedures, unknown
  versions or unknown programs. The server answers them itself, including
  the range of supported versions.
- **Timeouts.** `oncRPCClientSetTimeout()` sets the time allowed per call.
  A call that gets no answer finishes with `ONCRPC_ERR_TIMEDOUT`.
- **Errors are just outcomes.** Every call finishes in its callback, with
  `ONCRPC_OK` or an error; there is no separate error path.
