# oncRPCServerPort(3)

## NAME

oncRPCServerPort - port a server listens on

## SYNOPSIS

```c
#include <oncrpc.h>

uint16_t oncRPCServerPort(const oncRPCServer *srv, oncRPCProto proto);
```

## DESCRIPTION

`oncRPCServerPort()` returns the port of the server's first listener for
`proto`. It is mainly used after listening on port 0, to learn which port
the system chose.

## RETURN VALUE

The port, or 0 if the server has no TCP or UDP listener for `proto` (local
listeners have no port) or `srv` is `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void print_port(const oncRPCServer *server)
{
    printf("TCP port %u, UDP port %u\n",
           (unsigned)oncRPCServerPort(server, ONCRPC_PROTO_TCP),
           (unsigned)oncRPCServerPort(server, ONCRPC_PROTO_UDP));
}
```

## SEE ALSO

[oncRPCServerListen(3)](oncRPCServerListen.3.md)
