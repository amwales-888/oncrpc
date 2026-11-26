# oncRPCNetid(3)

## NAME

oncRPCNetid - rpcbind network id of a transport

## SYNOPSIS

```c
#include <oncrpc.h>

const char *oncRPCNetid(oncRPCProto proto, int family);
```

## DESCRIPTION

`oncRPCNetid()` returns the rpcbind network id for transport `proto` over
address family `family` (`AF_INET` or `AF_INET6`):

| `proto` | `AF_INET` | `AF_INET6` |
|---|---|---|
| `ONCRPC_PROTO_TCP` | `"tcp"` | `"tcp6"` |
| `ONCRPC_PROTO_UDP` | `"udp"` | `"udp6"` |
| `ONCRPC_PROTO_LOCAL` | `"local"` | `"local"` |

The family is ignored for local sockets.

## RETURN VALUE

A static string, or `NULL` for an unknown `proto`.

## EXAMPLE

Register a service listening on an arbitrary socket address:

```c
#include <oncrpc.h>

void register_addr(uv_loop_t *loop, const oncRPCAllocator *a, oncRPCProto proto,
                   const struct sockaddr *addr)
{
    char uaddr[ONCRPC_UADDR_MAX];
    const char *netid = oncRPCNetid(proto, addr->sa_family);

    if (netid != NULL && oncRPCUaddrFromSockaddr(addr, uaddr, sizeof(uaddr)) == 0)
        oncRPCRpcbSet(loop, a, 0x20000101, 1, netid, uaddr, NULL, NULL);
}
```

## SEE ALSO

[oncRPCRpcbSet(3)](oncRPCRpcbSet.3.md),
[oncRPCUaddrFromSockaddr(3)](oncRPCUaddrFromSockaddr.3.md)
