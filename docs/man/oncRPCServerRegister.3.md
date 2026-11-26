# oncRPCServerRegister(3)

## NAME

oncRPCServerRegister - serve a program version

## SYNOPSIS

```c
#include <oncrpc.h>

typedef void (*oncRPCDispatchFn)(oncRPCSvcReq *req);

oncRPCStat oncRPCServerRegister(oncRPCServer *srv, uint32_t prog, uint32_t vers,
                                oncRPCDispatchFn dispatch, void *ctx);
```

## DESCRIPTION

`oncRPCServerRegister()` makes the server hand every call to version
`vers` of program `prog` to `dispatch`. Registering the same program
version again replaces the dispatch routine and `ctx`.

`dispatch(req)` runs from the event loop for each call. `req` describes
it:

```c
typedef struct oncRPCSvcReq {
    uint32_t                   xid;
    uint32_t                   prog;
    uint32_t                   vers;
    uint32_t                   proc;
    oncRPCProto                proto;
    uint32_t                   credFlavor;
    const oncRPCAuthSysParams *authSys;
    void                      *ctx;
    oncRPCServer              *server;
    oncRPCSvcPriv             *priv;
    void                      *authCtx;
} oncRPCSvcReq;
```

`proc` is the procedure; `credFlavor` and `authSys` describe the
credentials (`authSys` is set for AUTH_SYS); `ctx` is the pointer given
here; `authCtx` is whatever an authenticator left (see
[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md)). `priv` belongs to
the library.

The dispatch routine decodes the arguments with
[oncRPCSvcDecodeArgs(3)](oncRPCSvcDecodeArgs.3.md) and finishes the request
with [oncRPCSvcReply(3)](oncRPCSvcReply.3.md), one of the error replies
such as [oncRPCSvcErrNoProc(3)](oncRPCSvcErrNoProc.3.md), or
[oncRPCSvcDrop(3)](oncRPCSvcDrop.3.md). It may also return without
finishing it and finish it later, from another callback: the request
stays valid until then. Every request must be finished exactly once.

Calls to a program the server does not serve are answered with
PROG_UNAVAIL, and calls to a version it does not serve with PROG_MISMATCH
and the range of versions it does serve, without the application being
involved.

Generated code provides `oncRPC<Program>V<n>Register(srv, ctx)`, which
calls this function with a generated dispatch routine; see
[oncrpcgen(1)](oncrpcgen.1.md).

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if `srv` or `dispatch` is `NULL` or the
server is closing; `ONCRPC_ERR_NOMEM` if the allocator failed.

## EXAMPLE

Serve two versions of a program from one routine, using `ctx` to tell
them apart:

```c
#include <oncrpc.h>

struct version_info {
    int  number;
    int  supportsBulk;
};

static void dispatch(oncRPCSvcReq *req)
{
    const struct version_info *v = req->ctx;

    if (req->proc == 0)
        oncRPCSvcReply(req, NULL, NULL);
    else if (req->proc == 5 && !v->supportsBulk)
        oncRPCSvcErrNoProc(req);
    else
        oncRPCSvcErrSystem(req);   /* real procedures omitted */
}

oncRPCStat serve(oncRPCServer *server)
{
    static const struct version_info v1 = { 1, 0 }, v2 = { 2, 1 };
    oncRPCStat st = oncRPCServerRegister(server, 0x20000200, 1, dispatch, (void *)&v1);
    if (st == ONCRPC_OK)
        st = oncRPCServerRegister(server, 0x20000200, 2, dispatch, (void *)&v2);
    return st;
}
```

## SEE ALSO

[oncRPCServerUnregister(3)](oncRPCServerUnregister.3.md),
[oncRPCSvcDecodeArgs(3)](oncRPCSvcDecodeArgs.3.md),
[oncRPCSvcReply(3)](oncRPCSvcReply.3.md),
[oncrpcgen(1)](oncrpcgen.1.md)
