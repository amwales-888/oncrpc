# oncRPCServerListen(3)

## NAME

oncRPCServerListen - accept calls on a transport and address

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCServerListen(oncRPCServer *srv, oncRPCProto proto,
                              const char *ip, uint16_t port);
```

## DESCRIPTION

`oncRPCServerListen()` makes the server accept calls over `proto` on the
numeric address `ip` and port `port`. It may be called several times, for
instance for TCP and UDP, or for IPv4 and IPv6.

- `ip` is a numeric IPv4 or IPv6 address: `"127.0.0.1"` or `"::1"` for
  this machine only, `NULL` or `"0.0.0.0"` for every IPv4 interface,
  `"::"` for every IPv6 interface. Host names are not accepted.
- `port` 0 picks any free port; find out which with
  [oncRPCServerPort(3)](oncRPCServerPort.3.md).
- For `ONCRPC_PROTO_LOCAL`, `ip` is the path of the Unix domain socket to
  create (on Windows, a pipe name such as `\\.\pipe\name`) and `port` is
  ignored. The socket file is removed when the server closes. One left
  behind by a program that crashed makes this call fail until it is
  deleted.

Listening starts at once; calls are accepted as soon as the loop runs.

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if `srv` is `NULL` or closing, `proto`
is unknown, or `ip` is not a numeric address; `ONCRPC_ERR_NOMEM` if the
allocator failed; otherwise a status for the failure reported by the
system (for example the address is already in use), usually
`ONCRPC_ERR_FAILED`.

## EXAMPLE

Listen on TCP and UDP on the same, freely chosen port:

```c
#include <stdio.h>
#include <oncrpc.h>

int listen_everywhere(oncRPCServer *server)
{
    uint16_t port;

    if (oncRPCServerListen(server, ONCRPC_PROTO_TCP, "0.0.0.0", 0) != ONCRPC_OK)
        return -1;
    port = oncRPCServerPort(server, ONCRPC_PROTO_TCP);
    if (oncRPCServerListen(server, ONCRPC_PROTO_UDP, "0.0.0.0", port) != ONCRPC_OK)
        return -1;
    printf("listening on port %u\n", (unsigned)port);
    return 0;
}
```

## SEE ALSO

[oncRPCServerPort(3)](oncRPCServerPort.3.md),
[oncRPCServerPmapRegister(3)](oncRPCServerPmapRegister.3.md),
[oncRPCSvcLocalCred(3)](oncRPCSvcLocalCred.3.md)
