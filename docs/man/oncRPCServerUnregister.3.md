# oncRPCServerUnregister(3)

## NAME

oncRPCServerUnregister - stop serving a program version

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCServerUnregister(oncRPCServer *srv, uint32_t prog, uint32_t vers);
```

## DESCRIPTION

`oncRPCServerUnregister()` removes version `vers` of program `prog` from
the server. New calls to it are answered as for any program version the
server does not serve. Requests already handed to the dispatch routine
are not affected and must still be finished.

It does not touch rpcbind; see
[oncRPCServerPmapUnregister(3)](oncRPCServerPmapUnregister.3.md).

## RETURN VALUE

`ONCRPC_OK`, or `ONCRPC_ERR_INVALID` if `srv` is `NULL` or the program
version was not registered.

## EXAMPLE

Retire version 1 once every client has moved to version 2:

```c
#include <oncrpc.h>

void retire_v1(oncRPCServer *server)
{
    oncRPCServerUnregister(server, 0x20000200, 1);
    /* Version 1 callers now get PROG_MISMATCH, listing version 2. */
}
```

## SEE ALSO

[oncRPCServerRegister(3)](oncRPCServerRegister.3.md)
