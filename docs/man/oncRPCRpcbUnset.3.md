# oncRPCRpcbUnset(3)

## NAME

oncRPCRpcbUnset - remove a service from the local rpcbind

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCRpcbUnset(uv_loop_t *loop, const oncRPCAllocator *alloc,
                           uint32_t prog, uint32_t vers, const char *netid,
                           oncRPCPmapCb cb, void *data);
```

## DESCRIPTION

`oncRPCRpcbUnset()` removes the registration of version `vers` of program
`prog` for network id `netid` from the rpcbind daemon on this machine,
with the `UNSET` procedure of rpcbind version 4 (falling back to 3).
`netid` `NULL` or `""` removes it for every network id. `cb(status,
value, data)` then runs; `value` is 1 if rpcbind removed something.

## RETURN VALUE

`ONCRPC_OK` if the request was sent (`cb` will run once), otherwise
`ONCRPC_ERR_INVALID` or `ONCRPC_ERR_NOMEM`.

## EXAMPLE

Replace an old registration: remove it, then register the new address:

```c
#include <oncrpc.h>

struct rereg {
    uv_loop_t             *loop;
    const oncRPCAllocator *alloc;
    char                   uaddr[ONCRPC_UADDR_MAX];
};

static void on_unset(oncRPCStat status, uint32_t removed, void *data)
{
    struct rereg *r = data;
    (void)status;
    (void)removed;
    oncRPCRpcbSet(r->loop, r->alloc, 0x20000101, 1, ONCRPC_NETID_TCP, r->uaddr, NULL, NULL);
}

void reregister(struct rereg *r)
{
    oncRPCRpcbUnset(r->loop, r->alloc, 0x20000101, 1, ONCRPC_NETID_TCP, on_unset, r);
}
```

## SEE ALSO

[oncRPCRpcbSet(3)](oncRPCRpcbSet.3.md),
[oncRPCServerPmapUnregister(3)](oncRPCServerPmapUnregister.3.md)
