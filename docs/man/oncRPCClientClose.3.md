# oncRPCClientClose(3)

## NAME

oncRPCClientClose - close a client

## SYNOPSIS

```c
#include <oncrpc.h>

typedef void (*oncRPCCloseCb)(void *data);

void oncRPCClientClose(oncRPCClient *clnt, oncRPCCloseCb cb, void *data);
```

## DESCRIPTION

`oncRPCClientClose()` starts closing the client `clnt`. Every call that has
not completed finishes with `ONCRPC_ERR_CANCELLED` (its callback runs),
the connection and the client's other libuv handles are closed, and the
client's memory is released. Then `cb`, if not `NULL`, runs with `data`.

Closing takes a turn or two of the event loop, because libuv closes
handles asynchronously. Once `oncRPCClientClose()` has been called, new
calls are refused with `ONCRPC_ERR_INVALID` and a second close is ignored.
The handle stays valid until its memory is released, just before `cb`
runs; do not use it after that. Calling `oncRPCClientClose()` from inside
one of the client's own callbacks is allowed.

A program whose loop runs `UV_RUN_DEFAULT` usually ends when its last
client and server have closed, because they hold the loop's last handles.

## EXAMPLE

Close the client once every call has finished:

```c
#include <stdio.h>
#include <oncrpc.h>

void on_closed(void *data)
{
    printf("client %s closed\n", (const char *)data);
}

void on_call(oncRPCClient *client, oncRPCStat status, void *res, void *data)
{
    (void)status;
    (void)res;
    (void)data;
    if (oncRPCClientPending(client) == 0)   /* this was the last one */
        oncRPCClientClose(client, on_closed, "main");
}
```

## SEE ALSO

[oncRPCClientCreate(3)](oncRPCClientCreate.3.md),
[oncRPCClientCancel(3)](oncRPCClientCancel.3.md),
[oncRPCClientPending(3)](oncRPCClientPending.3.md)
