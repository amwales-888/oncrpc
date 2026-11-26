# oncRPCUaddrFromSockaddr(3)

## NAME

oncRPCUaddrFromSockaddr - universal address of a socket address

## SYNOPSIS

```c
#include <oncrpc.h>

int oncRPCUaddrFromSockaddr(const struct sockaddr *addr, char *buf, size_t len);
```

## DESCRIPTION

`oncRPCUaddrFromSockaddr()` writes the rpcbind universal address of the
IPv4 or IPv6 socket address `addr` into `buf`, as a NUL-terminated
string: the address as text, then the port's high and low bytes, each in
decimal, all separated by dots. Port 1025 on 127.0.0.1 is
`"127.0.0.1.4.1"`; on `::1` it is `"::1.4.1"`.

A buffer of `ONCRPC_UADDR_MAX` bytes is always large enough.

## RETURN VALUE

0 on success; -1 if `addr` is not IPv4 or IPv6, an argument is `NULL`, or
the buffer is too small.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void print_uaddr(void)
{
    struct sockaddr_in sin;
    char uaddr[ONCRPC_UADDR_MAX];

    uv_ip4_addr("192.0.2.7", 2049, &sin);
    if (oncRPCUaddrFromSockaddr((const struct sockaddr *)&sin, uaddr, sizeof(uaddr)) == 0)
        printf("%s\n", uaddr);   /* 192.0.2.7.8.1 */
}
```

## SEE ALSO

[oncRPCUaddrToSockaddr(3)](oncRPCUaddrToSockaddr.3.md),
[oncRPCRpcbSet(3)](oncRPCRpcbSet.3.md)
