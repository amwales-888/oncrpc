# oncRPCPmapSet(3)

## NAME

oncRPCPmapSet - register a program with the local port mapper

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCPmapSet(uv_loop_t *loop, const oncRPCAllocator *alloc,
                         uint32_t prog, uint32_t vers, oncRPCProto proto,
                         uint16_t port, oncRPCPmapCb cb, void *data);
```

## DESCRIPTION

`oncRPCPmapSet()` tells the port mapper on this machine that version
`vers` of program `prog` is served over `proto` on `port`, with the `SET`
procedure of port mapper version 2. When it answers, `cb(status, value,
data)` runs; `value` is the port mapper's answer, 1 if it accepted the
registration and 0 if it refused it (usually because the program version
is already registered for that protocol).

Servers normally use
[oncRPCServerPmapRegister(3)](oncRPCServerPmapRegister.3.md) instead,
which registers every listener of a server, over the newest protocol the
daemon speaks. Use `oncRPCPmapSet()` to register a service the program
does not serve with an `oncRPCServer`.

Most systems only accept registrations from privileged processes or over
a local connection.

## RETURN VALUE

`ONCRPC_OK` if the request was sent (`cb` will run once);
`ONCRPC_ERR_INVALID` for a `proto` other than TCP or UDP;
`ONCRPC_ERR_NOMEM` if the allocator failed.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

static void on_set(oncRPCStat status, uint32_t accepted, void *data)
{
    (void)data;
    if (status != ONCRPC_OK)
        printf("port mapper unreachable: %s\n", oncRPCStrError(status));
    else if (!accepted)
        printf("port mapper refused the registration\n");
}

void announce(uv_loop_t *loop, const oncRPCAllocator *a, uint16_t port)
{
    oncRPCPmapSet(loop, a, 0x20000101, 1, ONCRPC_PROTO_TCP, port, on_set, NULL);
}
```

## SEE ALSO

[oncRPCPmapUnset(3)](oncRPCPmapUnset.3.md),
[oncRPCRpcbSet(3)](oncRPCRpcbSet.3.md),
[oncRPCServerPmapRegister(3)](oncRPCServerPmapRegister.3.md)
