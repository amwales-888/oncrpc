# oncRPCServerCreate(3)

## NAME

oncRPCServerCreate - create an RPC server

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCServerCreate(uv_loop_t *loop, const oncRPCAllocator *alloc,
                              oncRPCServer **srv);
```

## DESCRIPTION

`oncRPCServerCreate()` creates a server that runs on the libuv loop
`loop`, takes its memory from `alloc`, and stores it in `*srv`.

A new server does nothing yet. Set it up with:

1. [oncRPCServerListen(3)](oncRPCServerListen.3.md), once for each
   transport and address to accept calls on;
2. [oncRPCServerRegister(3)](oncRPCServerRegister.3.md) (or a generated
   `oncRPC<Program>V<n>Register` function), once for each program version
   it serves;
3. optionally [oncRPCServerPmapRegister(3)](oncRPCServerPmapRegister.3.md)
   to tell rpcbind where it is, and the limits, authentication and TLS
   functions.

Then run the loop. Each incoming call is handed to the dispatch routine of
its program version as an `oncRPCSvcReq`, which must be finished exactly
once with a reply, an error reply or a drop, either at once or later.
Calls to an unknown program or version, or with credentials the server does
not accept, are answered by the library without involving the application.

One server can listen on several transports and addresses and serve
several programs at once.

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if an argument is `NULL` or the
allocator lacks a function; `ONCRPC_ERR_NOMEM` if the allocator failed;
`ONCRPC_ERR_FAILED` if libuv could not set up the server's handles. On
failure `*srv` is `NULL`.

## EXAMPLE

A complete server for procedure 1 of program 0x20000101, which adds two
integers:

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

static void dispatch(oncRPCSvcReq *req)
{
    struct add_args *args;
    int32_t sum;

    switch (req->proc) {
    case 0:                                   /* the NULL procedure: a ping */
        oncRPCSvcReply(req, NULL, NULL);
        break;
    case 1:
        args = oncRPCSvcDecodeArgs(req, xdr_add_args, sizeof(*args));
        if (args == NULL) {
            oncRPCSvcErrDecode(req);
            break;
        }
        sum = args->a + args->b;
        oncRPCSvcReply(req, xdr_int, &sum);
        break;
    default:
        oncRPCSvcErrNoProc(req);
    }
}

int main(void)
{
    uv_loop_t *loop = uv_default_loop();
    oncRPCServer *server;

    if (oncRPCServerCreate(loop, &app_alloc, &server) != ONCRPC_OK ||
        oncRPCServerListen(server, ONCRPC_PROTO_TCP, "127.0.0.1", 5001) != ONCRPC_OK ||
        oncRPCServerRegister(server, 0x20000101, 1, dispatch, NULL) != ONCRPC_OK) {
        fprintf(stderr, "cannot start the server\n");
        return 1;
    }
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
```

## SEE ALSO

[oncRPCServerListen(3)](oncRPCServerListen.3.md),
[oncRPCServerRegister(3)](oncRPCServerRegister.3.md),
[oncRPCServerClose(3)](oncRPCServerClose.3.md),
[oncRPCSvcReply(3)](oncRPCSvcReply.3.md),
[liboncrpc(3)](liboncrpc.3.md)
