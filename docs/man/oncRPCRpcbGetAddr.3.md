# oncRPCRpcbGetAddr(3)

## NAME

oncRPCRpcbGetAddr - ask rpcbind for a service's address

## SYNOPSIS

```c
#include <oncrpc.h>

typedef void (*oncRPCRpcbAddrCb)(oncRPCStat status, const char *uaddr, void *data);

oncRPCStat oncRPCRpcbGetAddr(uv_loop_t *loop, const oncRPCAllocator *alloc,
                             const char *host, uint32_t prog, uint32_t vers,
                             const char *netid, oncRPCRpcbAddrCb cb, void *data);
```

## DESCRIPTION

`oncRPCRpcbGetAddr()` asks the rpcbind daemon on `host` where version
`vers` of program `prog` is served for the network id `netid`, with the
`GETADDR` procedure. It asks in rpcbind version 4 and falls back to
version 3. `host` `NULL` means 127.0.0.1.

rpcbind describes services with a network id - `"tcp"`, `"udp"`,
`"tcp6"`, `"udp6"` or `"local"` (the `ONCRPC_NETID_*` macros) - and a
universal address: the IP address as text followed by the port's two
bytes, so `"192.0.2.7.4.1"` is port 4 &times; 256 + 1 = 1025, and
`"::1.4.1"` the same port on IPv6. Unlike the port mapper, it can describe
IPv6 and local services.

When the answer arrives, `cb(status, uaddr, data)` runs. `uaddr` is only
valid during the callback; convert it with
[oncRPCUaddrToSockaddr(3)](oncRPCUaddrToSockaddr.3.md) or copy it.

rpcbind answers for the transport the question arrives on, not for the
`netid` in it. So the question goes over TCP for `"tcp"` and `"tcp6"` and
over UDP for `"udp"` and `"udp6"`, and over IPv4 or IPv6 as `host`
resolves (IPv4 preferred). To find a `tcp6` service, give an IPv6 host
such as `"::1"`.

## RETURN VALUE

`ONCRPC_OK` if the question was sent (`cb` will run once);
`ONCRPC_ERR_INVALID` if `netid` or another argument is missing;
`ONCRPC_ERR_NOMEM` if the allocator failed.

`cb` receives `ONCRPC_OK` with the address,
`ONCRPC_ERR_PROGNOTREGISTERED` (with `uaddr` `NULL`) if rpcbind knows no
such service, `ONCRPC_ERR_UNKNOWNHOST` if `host` cannot be resolved, or
`ONCRPC_ERR_PMAPFAILURE` if rpcbind could not be asked. For the `"local"`
network id the address is the socket's path.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

static void on_addr(oncRPCStat status, const char *uaddr, void *data)
{
    struct sockaddr_storage ss;
    (void)data;

    if (status != ONCRPC_OK) {
        printf("not found: %s\n", oncRPCStrError(status));
        return;
    }
    printf("served at %s\n", uaddr);
    if (oncRPCUaddrToSockaddr(uaddr, &ss) == 0 && ss.ss_family == AF_INET6)
        printf("(an IPv6 address)\n");
}

void find_service(uv_loop_t *loop, const oncRPCAllocator *a)
{
    oncRPCRpcbGetAddr(loop, a, "::1", 0x20000101, 1, ONCRPC_NETID_TCP6, on_addr, NULL);
}
```

## SEE ALSO

[oncRPCRpcbSet(3)](oncRPCRpcbSet.3.md),
[oncRPCUaddrToSockaddr(3)](oncRPCUaddrToSockaddr.3.md),
[oncRPCPmapGetPort(3)](oncRPCPmapGetPort.3.md),
[oncRPCClientCreate(3)](oncRPCClientCreate.3.md)
