# oncRPCRpcbSet(3)

## NAME

oncRPCRpcbSet - register a service with the local rpcbind

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCRpcbSet(uv_loop_t *loop, const oncRPCAllocator *alloc,
                         uint32_t prog, uint32_t vers, const char *netid,
                         const char *uaddr, oncRPCPmapCb cb, void *data);
```

## DESCRIPTION

`oncRPCRpcbSet()` registers version `vers` of program `prog` with the
rpcbind daemon on this machine, for network id `netid` at universal
address `uaddr` (see [oncRPCRpcbGetAddr(3)](oncRPCRpcbGetAddr.3.md) for
the format, and [oncRPCUaddrFromSockaddr(3)](oncRPCUaddrFromSockaddr.3.md)
to make one). It uses the `SET` procedure of rpcbind version 4, falling
back to 3. When rpcbind answers, `cb(status, value, data)` runs; `value` is
1 if the registration was accepted and 0 if it was refused (usually
because one exists already: remove it first with
[oncRPCRpcbUnset(3)](oncRPCRpcbUnset.3.md)).

Servers built on `oncRPCServer` normally use
[oncRPCServerPmapRegister(3)](oncRPCServerPmapRegister.3.md), which
registers every listener with the right network id and address.

## RETURN VALUE

`ONCRPC_OK` if the request was sent (`cb` will run once);
`ONCRPC_ERR_INVALID` if `netid` or `uaddr` is `NULL`; `ONCRPC_ERR_NOMEM`
if the allocator failed.

## EXAMPLE

Register a local-socket service:

```c
#include <stdio.h>
#include <oncrpc.h>

static void on_set(oncRPCStat status, uint32_t accepted, void *data)
{
    (void)data;
    if (status != ONCRPC_OK || !accepted)
        fprintf(stderr, "registration failed\n");
}

void register_local(uv_loop_t *loop, const oncRPCAllocator *a)
{
    oncRPCRpcbSet(loop, a, 0x20000109, 1, ONCRPC_NETID_LOCAL, "/run/myservice.sock",
                  on_set, NULL);
}
```

## SEE ALSO

[oncRPCRpcbUnset(3)](oncRPCRpcbUnset.3.md),
[oncRPCRpcbGetAddr(3)](oncRPCRpcbGetAddr.3.md),
[oncRPCServerPmapRegister(3)](oncRPCServerPmapRegister.3.md)
