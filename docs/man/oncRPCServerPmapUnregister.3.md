# oncRPCServerPmapUnregister(3)

## NAME

oncRPCServerPmapUnregister - remove a program version from rpcbind

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCServerPmapUnregister(oncRPCServer *srv, uint32_t prog, uint32_t vers,
                                      oncRPCServerPmapCb cb, void *data);
```

## DESCRIPTION

`oncRPCServerPmapUnregister()` removes version `vers` of program `prog`
from the rpcbind daemon on this machine, for every network id, then runs
`cb(srv, status, data)` if `cb` is not `NULL`.

Do it before closing the server, so that clients are not sent to an
address nobody listens on; wait for `cb` before calling
[oncRPCServerClose(3)](oncRPCServerClose.3.md).

## RETURN VALUE

`ONCRPC_OK` if the request was started (`cb` will run once), otherwise
`ONCRPC_ERR_INVALID` or `ONCRPC_ERR_NOMEM`.

## EXAMPLE

Unregister, then close:

```c
#include <oncrpc.h>

static void on_unregistered(oncRPCServer *server, oncRPCStat status, void *data)
{
    (void)status;
    (void)data;
    oncRPCServerClose(server, NULL, NULL);
}

void shut_down(oncRPCServer *server)
{
    if (oncRPCServerPmapUnregister(server, 0x20000101, 1, on_unregistered, NULL) != ONCRPC_OK)
        oncRPCServerClose(server, NULL, NULL);
}
```

## SEE ALSO

[oncRPCServerPmapRegister(3)](oncRPCServerPmapRegister.3.md),
[oncRPCServerClose(3)](oncRPCServerClose.3.md)
