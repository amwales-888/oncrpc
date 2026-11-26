# oncRPCClientPending(3)

## NAME

oncRPCClientPending - number of calls not yet completed

## SYNOPSIS

```c
#include <oncrpc.h>

size_t oncRPCClientPending(const oncRPCClient *clnt);
```

## DESCRIPTION

`oncRPCClientPending()` returns how many calls started on `clnt` have not
completed yet: those waiting to be sent, those waiting for a reply, and
those cancelled or failed whose callbacks have not run yet. Inside a
call's callback, that call is no longer counted.

It is useful for limiting how many calls an application keeps in flight,
and for closing a client when its work is done.

## RETURN VALUE

The number of calls, or 0 if `clnt` is `NULL`.

## EXAMPLE

Keep at most 32 calls in flight:

```c
#include <oncrpc.h>

int can_send_more(const oncRPCClient *client)
{
    return oncRPCClientPending(client) < 32;
}
```

## SEE ALSO

[oncRPCClientCall(3)](oncRPCClientCall.3.md),
[oncRPCClientClose(3)](oncRPCClientClose.3.md)
