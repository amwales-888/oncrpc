# oncRPCPmapUnset(3)

## NAME

oncRPCPmapUnset - remove a program from the local port mapper

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCPmapUnset(uv_loop_t *loop, const oncRPCAllocator *alloc,
                           uint32_t prog, uint32_t vers, oncRPCPmapCb cb, void *data);
```

## DESCRIPTION

`oncRPCPmapUnset()` removes every registration of version `vers` of
program `prog` from the port mapper on this machine (all protocols), with
the `UNSET` procedure of port mapper version 2. `cb(status, value, data)`
then runs; `value` is 1 if the port mapper removed something.

A server should remove its registrations when it stops, so that clients
are not sent to a port nobody listens on.

## RETURN VALUE

`ONCRPC_OK` if the request was sent (`cb` will run once), otherwise
`ONCRPC_ERR_INVALID` or `ONCRPC_ERR_NOMEM`.

## EXAMPLE

```c
#include <oncrpc.h>

static void on_unset(oncRPCStat status, uint32_t removed, void *data)
{
    (void)status;
    (void)removed;
    uv_stop((uv_loop_t *)data);   /* shutting down: done */
}

void withdraw(uv_loop_t *loop, const oncRPCAllocator *a)
{
    oncRPCPmapUnset(loop, a, 0x20000101, 1, on_unset, loop);
}
```

## SEE ALSO

[oncRPCPmapSet(3)](oncRPCPmapSet.3.md),
[oncRPCServerPmapUnregister(3)](oncRPCServerPmapUnregister.3.md)
