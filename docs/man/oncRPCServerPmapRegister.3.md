# oncRPCServerPmapRegister(3)

## NAME

oncRPCServerPmapRegister - register a server's listeners with rpcbind

## SYNOPSIS

```c
#include <oncrpc.h>

typedef void (*oncRPCServerPmapCb)(oncRPCServer *srv, oncRPCStat status, void *data);

oncRPCStat oncRPCServerPmapRegister(oncRPCServer *srv, uint32_t prog, uint32_t vers,
                                    oncRPCServerPmapCb cb, void *data);
```

## DESCRIPTION

`oncRPCServerPmapRegister()` registers version `vers` of program `prog`
with the rpcbind daemon on this machine, for every listener of the server,
so that clients can find it with port 0 in
[oncRPCClientCreate(3)](oncRPCClientCreate.3.md). Stale entries for the
program version are removed first.

Each listener is registered under its own network id and address: IPv4
TCP as `tcp`, IPv6 UDP as `udp6`, a local socket as `local`, and so on. The
newest protocol the daemon speaks is used: rpcbind version 4 or 3, or the
port mapper (version 2), which only knows IPv4 TCP and UDP.

When every registration has been made, `cb(srv, status, data)` runs, if
`cb` is not `NULL`, with `ONCRPC_OK` or the first failure.

Call it after the listeners are set up. Most systems accept registrations
only from privileged processes or over a local connection.

## RETURN VALUE

`ONCRPC_OK` if the registration was started (`cb` will run once);
`ONCRPC_ERR_INVALID` if `srv` is `NULL`, closing, or has no listeners;
`ONCRPC_ERR_NOMEM` if the allocator failed.

`cb` receives `ONCRPC_ERR_PMAPFAILURE` if the daemon could not be reached
or refused a registration, including IPv6 listeners when the daemon only
speaks port mapper version 2.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

static void on_registered(oncRPCServer *server, oncRPCStat status, void *data)
{
    (void)server;
    (void)data;
    if (status != ONCRPC_OK)
        fprintf(stderr, "rpcbind registration failed: %s\n", oncRPCStrError(status));
}

void announce(oncRPCServer *server)
{
    oncRPCServerPmapRegister(server, 0x20000101, 1, on_registered, NULL);
}
```

## SEE ALSO

[oncRPCServerPmapUnregister(3)](oncRPCServerPmapUnregister.3.md),
[oncRPCRpcbSet(3)](oncRPCRpcbSet.3.md),
[oncRPCServerListen(3)](oncRPCServerListen.3.md)
