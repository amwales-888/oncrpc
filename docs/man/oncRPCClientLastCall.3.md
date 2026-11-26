# oncRPCClientLastCall(3)

## NAME

oncRPCClientLastCall - id of the call just started

## SYNOPSIS

```c
#include <oncrpc.h>

typedef uint64_t oncRPCCallId;

oncRPCCallId oncRPCClientLastCall(const oncRPCClient *clnt);
```

## DESCRIPTION

Every call a client starts gets an id, unique for that client and never 0.
`oncRPCClientLastCall()` returns the id of the call most recently started
on `clnt`, by [oncRPCClientCall(3)](oncRPCClientCall.3.md) or by a
generated stub. Read it straight after starting the call; the id is what
[oncRPCClientCancel(3)](oncRPCClientCancel.3.md) takes.

The id stays the same if the call is sent again, for instance with fresh
credentials, although its transaction id on the wire changes.

## RETURN VALUE

The id, or 0 if no call has been started on `clnt` or `clnt` is `NULL`.

## EXAMPLE

```c
#include <oncrpc.h>

static int xdr_int(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrInt32(xdrs, (int32_t *)obj);
}

static int32_t answer;

oncRPCCallId start_lookup(oncRPCClient *client, int32_t key, oncRPCCallCb cb)
{
    if (oncRPCClientCall(client, 1, xdr_int, &key, xdr_int, &answer, cb, NULL) != ONCRPC_OK)
        return 0;
    return oncRPCClientLastCall(client);   /* keep it to cancel the call later */
}
```

## SEE ALSO

[oncRPCClientCancel(3)](oncRPCClientCancel.3.md),
[oncRPCClientCall(3)](oncRPCClientCall.3.md)
