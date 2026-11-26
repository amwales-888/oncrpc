# oncRPCSvcSetAbandonCb(3)

## NAME

oncRPCSvcSetAbandonCb - be told when a request's caller goes away

## SYNOPSIS

```c
#include <oncrpc.h>

typedef void (*oncRPCSvcAbandonCb)(oncRPCSvcReq *req, void *data);

void oncRPCSvcSetAbandonCb(oncRPCSvcReq *req, oncRPCSvcAbandonCb cb, void *data);
```

## DESCRIPTION

`oncRPCSvcSetAbandonCb()` asks for `cb(req, data)` to run, once, if `req`
becomes abandoned before it is finished: its connection closes or the
server starts closing (see [oncRPCSvcAbandoned(3)](oncRPCSvcAbandoned.3.md)).
The callback typically stops the work started for the request and drops
it.

- `cb` runs from the event loop, never from inside a library call, not
  even from inside [oncRPCServerClose(3)](oncRPCServerClose.3.md).
- If the request is already abandoned, `cb` runs soon after this call.
- It does not run for a request finished before it would have.
- Setting it again replaces the callback; `NULL` cancels it.
- Calling it on a finished request does nothing.

A closing server waits for every outstanding request, so a server that
answers later should finish its requests in this callback; otherwise
`oncRPCServerClose()` never completes.

## EXAMPLE

A request answered by a timer, which stops when the caller hangs up:

```c
#include <stdlib.h>
#include <oncrpc.h>

struct pending {
    uv_timer_t    timer;
    oncRPCSvcReq *req;
};

static void on_closed(uv_handle_t *h)
{
    free(h->data);
}

static void on_ready(uv_timer_t *t)
{
    struct pending *p = t->data;
    oncRPCSvcReply(p->req, NULL, NULL);
    uv_close((uv_handle_t *)&p->timer, on_closed);
}

static void on_abandoned(oncRPCSvcReq *req, void *data)
{
    struct pending *p = data;
    oncRPCSvcDrop(req);
    uv_close((uv_handle_t *)&p->timer, on_closed);   /* also stops it */
}

void slow_proc(uv_loop_t *loop, oncRPCSvcReq *req)
{
    struct pending *p = malloc(sizeof(*p));

    if (p == NULL) {
        oncRPCSvcErrSystem(req);
        return;
    }
    p->req = req;
    uv_timer_init(loop, &p->timer);
    p->timer.data = p;
    uv_timer_start(&p->timer, on_ready, 5000, 0);
    oncRPCSvcSetAbandonCb(req, on_abandoned, p);
}
```

`examples/11_auth` in the source tree uses the same pattern.

## SEE ALSO

[oncRPCSvcAbandoned(3)](oncRPCSvcAbandoned.3.md),
[oncRPCSvcDrop(3)](oncRPCSvcDrop.3.md),
[oncRPCServerClose(3)](oncRPCServerClose.3.md)
