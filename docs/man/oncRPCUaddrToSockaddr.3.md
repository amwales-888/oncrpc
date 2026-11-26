# oncRPCUaddrToSockaddr(3)

## NAME

oncRPCUaddrToSockaddr - socket address of a universal address

## SYNOPSIS

```c
#include <oncrpc.h>

int oncRPCUaddrToSockaddr(const char *uaddr, struct sockaddr_storage *addr);
```

## DESCRIPTION

`oncRPCUaddrToSockaddr()` parses the rpcbind universal address `uaddr`
(for example `"192.0.2.7.8.1"` or `"::1.4.1"`; see
[oncRPCUaddrFromSockaddr(3)](oncRPCUaddrFromSockaddr.3.md)) and stores the
corresponding IPv4 or IPv6 socket address, port included, in `*addr`.
`addr->ss_family` tells which it is.

Universal addresses come from other machines, so the function checks the
text strictly: the two port parts must be numbers from 0 to 255, and the
address part must be a valid IPv4 or IPv6 address.

## RETURN VALUE

0 on success; -1 if the text is malformed or an argument is `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

int port_of_uaddr(const char *uaddr)
{
    struct sockaddr_storage ss;

    if (oncRPCUaddrToSockaddr(uaddr, &ss) != 0)
        return -1;
    if (ss.ss_family == AF_INET)
        return ntohs(((struct sockaddr_in *)&ss)->sin_port);
    return ntohs(((struct sockaddr_in6 *)&ss)->sin6_port);
}
```

## SEE ALSO

[oncRPCUaddrFromSockaddr(3)](oncRPCUaddrFromSockaddr.3.md),
[oncRPCRpcbGetAddr(3)](oncRPCRpcbGetAddr.3.md)
