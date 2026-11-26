# liboncrpc(3)

## NAME

liboncrpc - asynchronous ONC RPC and XDR on libuv

## SYNOPSIS

```c
#include <oncrpc.h>     /* RPC clients and servers; includes xdr.h */
#include <xdr.h>        /* XDR on its own */
#include <xspec.h>      /* the .x language parser and code generator */
```

Link with `-loncrpc`, plus libuv and OpenSSL 3 when linking the static
library (see `BUILDING.md`).

## DESCRIPTION

liboncrpc implements ONC RPC version 2 (RFC 5531), XDR (RFC 4506), the
port mapper and rpcbind protocols (RFC 1833) and RPC-with-TLS (RFC 9289)
in C99. It runs on a libuv event loop supplied by the application, and
never blocks.

### Asynchronous operation

No function waits for the network. A function that starts network work
(creating a client, making a call, registering with rpcbind, closing a
handle) returns at once, and a callback reports the outcome later, while
the application runs the loop with `uv_run()`.

- A function that starts an operation returns an `oncRPCStat`. `ONCRPC_OK`
  means it started and its callback will run exactly once. Any other value
  means it did not start, and no callback will follow.
- Callbacks always run from the event loop, never from inside the function
  that started the operation. Code after a call such as
  `oncRPCClientCall()` therefore always runs before that call's callback.
- Many operations can be in progress at once: one client can have any
  number of calls outstanding, and one server any number of requests.

### Memory

The library never calls `malloc()`, `realloc()` or `free()`. Every client,
server, configuration and decoded value takes its memory from an
`oncRPCAllocator` supplied by the application (see
[oncRPCAlloc(3)](oncRPCAlloc.3.md)). Who owns what:

| What | Owner |
|---|---|
| Call arguments (client) | the caller; encoded before `oncRPCClientCall()` returns |
| Call results (client) | the caller's object, filled in by the library; free with [oncRPCXdrFree(3)](oncRPCXdrFree.3.md) on success |
| Request arguments (server) | the request; released when it is finished |
| Reply results (server) | the caller; encoded before `oncRPCSvcReply()` returns |
| Clients, servers | the library, until the close callback |
| TLS configurations | shared: each user holds a reference |

### Handles

Clients (`oncRPCClient`) and servers (`oncRPCServer`) are closed with
[oncRPCClientClose(3)](oncRPCClientClose.3.md) and
[oncRPCServerClose(3)](oncRPCServerClose.3.md). Closing takes effect over
the next turns of the loop, and the close callback is the last thing that
happens. When every handle on the loop is closed, `uv_run()` returns.

### Errors

Functions return `oncRPCStat` codes; [oncRPCStrError(3)](oncRPCStrError.3.md)
describes them, and [oncRPCClientLastError(3)](oncRPCClientLastError.3.md)
gives the details of a failed call. XDR routines return non-zero on
success and 0 on failure.

### Threads

Nothing is thread-safe. Use each client and server only from the thread
that runs its loop. Programs with several threads run one loop per thread
or pass work to the loop's thread (for instance with `uv_async_send()`).

### Signals

A peer that closes its connection while data is being written makes the
system send SIGPIPE on Unix-like systems, which ends the program by
default. Programs using liboncrpc should ignore it:
`signal(SIGPIPE, SIG_IGN)`.

### Code generation

Most programs describe their interface in a `.x` file and let
[oncrpcgen(1)](oncrpcgen.1.md) generate the types, XDR routines, typed
client stubs and server dispatch code, so that they rarely call
`oncRPCClientCall()` or `oncRPCSvcDecodeArgs()` directly.

## FUNCTIONS

See [the index](README.md) for every page, grouped by topic:
memory, XDR streams, XDR types, clients, port mapper and rpcbind, servers,
server requests, TLS, and the `.x` parser.

## STANDARDS

RFC 5531 (RPC version 2), RFC 4506 (XDR), RFC 1833 (port mapper and
rpcbind), RFC 9289 (RPC-with-TLS).

## EXAMPLE

The smallest useful pair, a client and server for one procedure, is in
[oncRPCClientCreate(3)](oncRPCClientCreate.3.md) and
[oncRPCServerCreate(3)](oncRPCServerCreate.3.md). `docs/TUTORIAL.md` walks
through writing a service from scratch, and `examples/` in the source tree
has eleven complete programs.

## SEE ALSO

[oncrpcgen(1)](oncrpcgen.1.md),
[oncRPCClientCreate(3)](oncRPCClientCreate.3.md),
[oncRPCServerCreate(3)](oncRPCServerCreate.3.md),
[oncRPCXdrInitEncode(3)](oncRPCXdrInitEncode.3.md),
[oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md)
