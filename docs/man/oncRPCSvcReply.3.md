# oncRPCSvcReply(3)

## NAME

oncRPCSvcReply - send a successful reply

## SYNOPSIS

```c
#include <oncrpc.h>

int oncRPCSvcReply(oncRPCSvcReq *req, oncRPCXdrProc xres, const void *res);
```

## DESCRIPTION

`oncRPCSvcReply()` sends a successful reply to `req`, with the results
`res` encoded by `xres` (`NULL` for a procedure that returns nothing), and
finishes the request.

`res` is encoded before the function returns and is not kept, so it may be
a local variable, and the caller may free it straight after.

It may be called inside the dispatch routine or at any later time - from a
timer, from the callback of another RPC, from a database's completion
callback - as long as the request has not been finished. If the caller's
connection has closed meanwhile, the reply is discarded, but the request
is still finished (see [oncRPCSvcAbandoned(3)](oncRPCSvcAbandoned.3.md)).

Over UDP, the reply is also kept in the duplicate-request cache, to answer
resends of the request.

Once finished, a request may be freed at once: do not use `req` again.

Generated code provides a typed function per procedure,
`oncRPC<Proc>V<n>Reply(req, &result)`, which calls this one.

## RETURN VALUE

Non-zero if the reply was encoded and sent. 0 if `req` was already
finished, or if `xres` failed; in that case a SYSTEM_ERR reply is sent
instead and the request is finished anyway.

## EXAMPLE

Answer a request one second after it arrived:

```c
#include <stdlib.h>
#include <oncrpc.h>

struct delayed {
    uv_timer_t    timer;
    oncRPCSvcReq *req;
};

static int xdr_u32(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrUint32(xdrs, (uint32_t *)obj);
}

static void on_closed(uv_handle_t *h)
{
    free(h->data);
}

static void on_timer(uv_timer_t *t)
{
    struct delayed *d = t->data;
    uint32_t answer = 42;

    oncRPCSvcReply(d->req, xdr_u32, &answer);
    uv_close((uv_handle_t *)&d->timer, on_closed);
}

void answer_later(uv_loop_t *loop, oncRPCSvcReq *req)
{
    struct delayed *d = malloc(sizeof(*d));

    if (d == NULL) {
        oncRPCSvcErrSystem(req);
        return;
    }
    d->req = req;
    uv_timer_init(loop, &d->timer);
    d->timer.data = d;
    uv_timer_start(&d->timer, on_timer, 1000, 0);
    /* returning without finishing req is fine */
}
```

## SEE ALSO

[oncRPCSvcDecodeArgs(3)](oncRPCSvcDecodeArgs.3.md),
[oncRPCSvcDrop(3)](oncRPCSvcDrop.3.md),
[oncRPCSvcErrSystem(3)](oncRPCSvcErrSystem.3.md),
[oncRPCSvcSetAbandonCb(3)](oncRPCSvcSetAbandonCb.3.md)
