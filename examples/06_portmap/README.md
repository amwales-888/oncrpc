# 06_portmap: finding servers through the port mapper

| File       | What it is                                                    |
|------------|---------------------------------------------------------------|
| `clock.x`  | `unsigned hyper GET_TIME(void)`                               |
| `server.c` | listens on any free TCP and UDP port, registers with rpcbind, unregisters on Ctrl-C |
| `client.c` | looks the server up by program number over TCP and UDP        |

This example needs the port mapper, `rpcbind`, running on the server's
machine (`sudo rpcbind`; on most Linux distributions it is the `rpcbind`
package). Depending on its configuration, registering may need root.

```sh
build/linux/release/examples/06_portmap/server &
rpcinfo -p | grep 536871174          # the registration (0x20000106)
build/linux/release/examples/06_portmap/client
# client: TCP client is connected
# client: UDP client is connected
# client: over TCP the server says it is 2026-11-15 11:40:02 UTC
# client: over UDP the server says it is 2026-11-15 11:40:02 UTC
kill -INT %1
# server: shutting down
# server: unregistered (success)
# server: closed
```

Things to notice:

- **Finding the port.** Port `0` in `oncRPCClientCreate()` means "ask the
  port mapper". The lookup happens asynchronously, before connecting.
- **Ready callback.** The optional ready callback (`on_ready`) says when the
  client is usable, or why it is not.
- **Registering.** `oncRPCServerPmapRegister()` registers every listener.
  Call it after `oncRPCServerListen()`.
- **Clean shutdown.** The server reacts to signals through `uv_signal_t`
  watchers on its loop. It unregisters, then closes; when nothing is left
  open, `uv_run()` returns.
- **No void arguments.** `GET_TIME(void)` produces a stub with no argument
  parameter: `get_time_1(client, &result, cb, data)`.
