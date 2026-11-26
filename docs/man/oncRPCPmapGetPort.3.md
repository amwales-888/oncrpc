# oncRPCPmapGetPort(3)

## NAME

oncRPCPmapGetPort - ask a port mapper for a program's port

## SYNOPSIS

```c
#include <oncrpc.h>

typedef void (*oncRPCPmapCb)(oncRPCStat status, uint32_t value, void *data);

oncRPCStat oncRPCPmapGetPort(uv_loop_t *loop, const oncRPCAllocator *alloc,
                             const char *host, uint32_t prog, uint32_t vers,
                             oncRPCProto proto, oncRPCPmapCb cb, void *data);
```

## DESCRIPTION

`oncRPCPmapGetPort()` asks the port mapper on `host` (port 111) which
port version `vers` of program `prog` uses over `proto`, with the
`GETPORT` procedure of port mapper version 2. `host` `NULL` means
127.0.0.1.

When the answer arrives, `cb(status, value, data)` runs with the port in
`value`.

Version 2 only knows IPv4 TCP and UDP services. Clients rarely need this
function: [oncRPCClientCreate(3)](oncRPCClientCreate.3.md) with port 0
looks the port up itself, using rpcbind versions 4 and 3 first. For IPv6
services use [oncRPCRpcbGetAddr(3)](oncRPCRpcbGetAddr.3.md).

## RETURN VALUE

`ONCRPC_OK` if the question was sent (`cb` will run once);
`ONCRPC_ERR_INVALID` for a `proto` other than TCP or UDP, or missing
arguments; `ONCRPC_ERR_NOMEM` if the allocator failed.

`cb` receives `ONCRPC_OK` with the port, `ONCRPC_ERR_PROGNOTREGISTERED` if
the port mapper knows no such program (port 0), `ONCRPC_ERR_UNKNOWNHOST`
if `host` cannot be resolved, or `ONCRPC_ERR_PMAPFAILURE` if the port
mapper could not be asked or gave a nonsensical answer.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

static void on_port(oncRPCStat status, uint32_t port, void *data)
{
    if (status == ONCRPC_OK)
        printf("%s is on port %u\n", (const char *)data, (unsigned)port);
    else
        printf("%s: %s\n", (const char *)data, oncRPCStrError(status));
}

void find_nfs(uv_loop_t *loop, const oncRPCAllocator *a)
{
    oncRPCPmapGetPort(loop, a, "fileserver", 100003, 3, ONCRPC_PROTO_TCP, on_port, "NFS v3");
}
```

## SEE ALSO

[oncRPCRpcbGetAddr(3)](oncRPCRpcbGetAddr.3.md),
[oncRPCPmapSet(3)](oncRPCPmapSet.3.md),
[oncRPCClientCreate(3)](oncRPCClientCreate.3.md)
