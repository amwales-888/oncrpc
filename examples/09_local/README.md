# 09_local: a service on a local socket

| File       | What it is                                                         |
|------------|--------------------------------------------------------------------|
| `local.x`  | the interface: `string WHO_AM_I(void)`                             |
| `server.c` | listens on a local socket and tells each caller who it is          |
| `client.c` | connects to the socket and prints the answer                       |

```sh
build/linux/release/examples/09_local/server &
build/linux/release/examples/09_local/client
# server: listening on /tmp/oncrpc-09.sock
# server: WHO_AM_I from uid 1000, gid 1000
# client: the server sees uid 1000, gid 1000
kill %1
```

Both take an optional path (`server /run/myapp/rpc.sock`). On Windows the
default is the pipe `\\.\pipe\oncrpc-09`.

Things to notice:

- **A path instead of a host and port.** `ONCRPC_PROTO_LOCAL` takes the
  socket's path where TCP and UDP take an address, in both
  `oncRPCServerListen()` and `oncRPCClientCreate()`. Messages are framed as
  over TCP, and calls, replies and errors work the same.
- **Who is calling.** `oncRPCSvcLocalCred()` gives the caller's user and
  group ids as the operating system reports them (Linux, macOS and the
  BSDs), so a server can decide what each user may do without passwords
  or tokens. Windows does not report them.
- **Who may connect.** Only programs on this machine can reach the socket,
  and its file permissions decide which ones: put it in a directory only
  the right users can enter.
- **Clean shutdown.** Closing the server removes the socket file. A server
  that is killed outright leaves the file behind, and the next one cannot
  listen there until it is deleted; that is why this server closes on
  SIGINT and SIGTERM.
- **Finding it through rpcbind.** `oncRPCServerPmapRegister()` registers a
  local listener under the network id `local` with its path, and
  `oncRPCRpcbGetAddr(..., ONCRPC_NETID_LOCAL, ...)` gives the path back.
