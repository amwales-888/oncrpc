# oncRPCClientCreate(3)

## NAME

oncRPCClientCreate - create a client for a remote program

## SYNOPSIS

```c
#include <oncrpc.h>

typedef void (*oncRPCClientReadyCb)(oncRPCClient *clnt, oncRPCStat status, void *data);

oncRPCStat oncRPCClientCreate(uv_loop_t *loop, const oncRPCAllocator *alloc,
                              const char *host, uint16_t port, uint32_t prog,
                              uint32_t vers, oncRPCProto proto,
                              oncRPCClientReadyCb readyCb, void *data,
                              oncRPCClient **clnt);
```

## DESCRIPTION

`oncRPCClientCreate()` creates a client for version `vers` of program
`prog` on `host`, using transport `proto`, and stores it in `*clnt`. The
client runs on the libuv loop `loop` and takes all its memory from
`alloc`.

The function returns at once. Name resolution, the port lookup and the
connection happen in the background while the loop runs. Calls can be
started straight away with [oncRPCClientCall(3)](oncRPCClientCall.3.md) or
a generated stub: they wait in a queue until the client is ready.

- `host` is a host name or a numeric IPv4 or IPv6 address. For
  `ONCRPC_PROTO_LOCAL` it is the path of a Unix domain socket (on Windows,
  a pipe name such as `\\.\pipe\name`).
- `port` is the server's port. If it is 0, the client asks the rpcbind (or
  port mapper) daemon on `host` where the program is: rpcbind version 4 or
  3 first, then the port mapper (version 2). It is ignored for local
  sockets.
- `proto` is `ONCRPC_PROTO_TCP`, `ONCRPC_PROTO_UDP` or
  `ONCRPC_PROTO_LOCAL`.
- `readyCb`, if not `NULL`, runs once with the outcome of setting up the
  client: `ONCRPC_OK` when it is ready, or the reason it failed. It is
  optional because the calls report failures too.
- `data` is passed to `readyCb` and can be read back at any time with
  [oncRPCClientData(3)](oncRPCClientData.3.md).

Like every callback in liboncrpc, `readyCb` runs from the event loop,
never from inside the function that started the work.

A client is not tied to one connection. If a TCP connection is lost, the
calls already sent on it fail with `ONCRPC_ERR_CANTRECV`, and the client
connects again the next time it has calls to send. If connecting fails,
the waiting calls fail with `ONCRPC_ERR_CANTCONNECT` and the next call
tries again. If the host name cannot be resolved, or the port lookup
fails, the client has failed for good: waiting calls fail with that
status, and later calls are refused with it.

Close the client with [oncRPCClientClose(3)](oncRPCClientClose.3.md).

## RETURN VALUE

`ONCRPC_OK` if the client was created. Otherwise no client exists,
`*clnt` is `NULL` and `readyCb` will not run:

- `ONCRPC_ERR_INVALID`: `loop`, `alloc`, `host` or `clnt` is `NULL`, the
  allocator lacks a function, or `proto` is not one of the three
  transports.
- `ONCRPC_ERR_NOMEM`: the allocator failed.
- `ONCRPC_ERR_FAILED`: libuv could not set up the client's timer.

Errors found later (`ONCRPC_ERR_UNKNOWNHOST`, `ONCRPC_ERR_CANTCONNECT`,
`ONCRPC_ERR_PROGNOTREGISTERED`, `ONCRPC_ERR_PMAPFAILURE`, ...) are
reported to `readyCb` and to the calls.

## EXAMPLE

A complete client that calls procedure 1 of program 0x20000101, which
adds two integers:

```c
#include <stdio.h>
#include <stdlib.h>
#include <oncrpc.h>

static void *app_realloc(void *ctx, void *p, size_t n) { (void)ctx; return realloc(p, n); }
static void app_free(void *ctx, void *p) { (void)ctx; free(p); }
static const oncRPCAllocator app_alloc = { app_realloc, app_free, NULL };

struct add_args { int32_t a, b; };

static int xdr_add_args(oncRPCXdr *xdrs, void *obj)
{
    struct add_args *p = obj;
    return oncRPCXdrInt32(xdrs, &p->a) && oncRPCXdrInt32(xdrs, &p->b);
}

static int xdr_int(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrInt32(xdrs, (int32_t *)obj);
}

static int32_t sum;   /* must outlive the call */

static void on_ready(oncRPCClient *client, oncRPCStat status, void *data)
{
    (void)client;
    (void)data;
    if (status != ONCRPC_OK)
        fprintf(stderr, "cannot reach the server: %s\n", oncRPCStrError(status));
}

static void on_add(oncRPCClient *client, oncRPCStat status, void *res, void *data)
{
    (void)res;
    (void)data;
    if (status == ONCRPC_OK)
        printf("2 + 3 = %d\n", (int)sum);
    oncRPCClientClose(client, NULL, NULL);
}

int main(void)
{
    uv_loop_t *loop = uv_default_loop();
    struct add_args args = { 2, 3 };
    oncRPCClient *client;

    if (oncRPCClientCreate(loop, &app_alloc, "127.0.0.1", 5001, 0x20000101, 1,
                           ONCRPC_PROTO_TCP, on_ready, NULL, &client) != ONCRPC_OK)
        return 1;
    if (oncRPCClientCall(client, 1, xdr_add_args, &args, xdr_int, &sum, on_add,
                         NULL) != ONCRPC_OK)
        oncRPCClientClose(client, NULL, NULL);
    uv_run(loop, UV_RUN_DEFAULT);   /* returns once the client has closed */
    uv_loop_close(loop);
    return 0;
}
```

## SEE ALSO

[oncRPCClientCall(3)](oncRPCClientCall.3.md),
[oncRPCClientClose(3)](oncRPCClientClose.3.md),
[oncRPCClientSetTls(3)](oncRPCClientSetTls.3.md),
[oncRPCServerCreate(3)](oncRPCServerCreate.3.md),
[liboncrpc(3)](liboncrpc.3.md)
