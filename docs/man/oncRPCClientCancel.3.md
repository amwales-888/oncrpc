# oncRPCClientCancel(3)

## NAME

oncRPCClientCancel - cancel one call

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCClientCancel(oncRPCClient *clnt, oncRPCCallId id);
```

## DESCRIPTION

`oncRPCClientCancel()` cancels the outstanding call `id` (from
[oncRPCClientLastCall(3)](oncRPCClientLastCall.3.md)). Its callback runs
with `ONCRPC_ERR_CANCELLED` soon after, from the event loop, never inside
this function. A reply that arrives later is ignored.

A call that has not been sent yet is never sent, and over UDP a cancelled
call is not resent. But a call that was already sent may have reached the
server and been carried out: cancelling only means the client stops
waiting for the answer.

Other calls on the client are not affected. To cancel every call, close
the client ([oncRPCClientClose(3)](oncRPCClientClose.3.md)).

## RETURN VALUE

- `ONCRPC_OK`: the call will complete with `ONCRPC_ERR_CANCELLED`.
- `ONCRPC_ERR_INVALID`: `clnt` is `NULL`, `id` is 0 or unknown, or the
  call has already completed, been cancelled, or failed. This includes a
  call cancelling itself from its own callback, since it has completed by
  then.

## EXAMPLE

Cancel a lookup when the user gives up, after one second:

```c
#include <stdio.h>
#include <oncrpc.h>

struct lookup {
    oncRPCClient *client;
    oncRPCCallId  id;
    uv_timer_t    timer;
};

static void on_give_up(uv_timer_t *t)
{
    struct lookup *l = t->data;
    if (oncRPCClientCancel(l->client, l->id) == ONCRPC_OK)
        printf("lookup cancelled\n");   /* its callback will say so too */
}

void limit_lookup(uv_loop_t *loop, struct lookup *l)
{
    uv_timer_init(loop, &l->timer);
    l->timer.data = l;
    uv_timer_start(&l->timer, on_give_up, 1000, 0);
}
```

## SEE ALSO

[oncRPCClientLastCall(3)](oncRPCClientLastCall.3.md),
[oncRPCClientClose(3)](oncRPCClientClose.3.md),
[oncRPCClientSetTimeout(3)](oncRPCClientSetTimeout.3.md)
