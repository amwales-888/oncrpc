# oncRPCServerClose(3)

## NAME

oncRPCServerClose - close a server

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCServerClose(oncRPCServer *srv, oncRPCCloseCb cb, void *data);
```

## DESCRIPTION

`oncRPCServerClose()` starts closing the server `srv`: it stops listening,
closes every connection, and removes the socket files of local listeners.
When every handle has closed and every outstanding request has been
finished, the server's memory is released and `cb(data)` runs, if `cb` is
not `NULL`.

Requests that are still being handled (answered later) stay valid and
must still be finished, but their replies go nowhere. Each of them becomes
abandoned: [oncRPCSvcAbandoned(3)](oncRPCSvcAbandoned.3.md) returns
non-zero, and the callback set with
[oncRPCSvcSetAbandonCb(3)](oncRPCSvcSetAbandonCb.3.md) runs. A server
that answers later should drop its requests there, or the close never
completes.

A second close is ignored. Closing does not remove the server's rpcbind
registrations; do that first with
[oncRPCServerPmapUnregister(3)](oncRPCServerPmapUnregister.3.md).

## EXAMPLE

Close the server when the program receives SIGINT:

```c
#include <stdio.h>
#include <oncrpc.h>

static oncRPCServer *server;
static uv_signal_t sigint;

static void on_closed(void *data)
{
    (void)data;
    printf("server closed\n");
}

static void on_sigint(uv_signal_t *sig, int signum)
{
    (void)signum;
    uv_close((uv_handle_t *)sig, NULL);
    oncRPCServerClose(server, on_closed, NULL);   /* then uv_run returns */
}

void stop_on_sigint(uv_loop_t *loop, oncRPCServer *srv)
{
    server = srv;
    uv_signal_init(loop, &sigint);
    uv_signal_start(&sigint, on_sigint, SIGINT);
}
```

## SEE ALSO

[oncRPCServerCreate(3)](oncRPCServerCreate.3.md),
[oncRPCSvcSetAbandonCb(3)](oncRPCSvcSetAbandonCb.3.md),
[oncRPCServerPmapUnregister(3)](oncRPCServerPmapUnregister.3.md)
