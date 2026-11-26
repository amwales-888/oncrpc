# oncRPCServerGetLimits(3)

## NAME

oncRPCServerGetLimits - read a server's limits

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCServerGetLimits(const oncRPCServer *srv, oncRPCServerLimits *limits);
```

## DESCRIPTION

`oncRPCServerGetLimits()` copies the server's current limits into
`*limits`. The structure is described in
[oncRPCServerSetLimits(3)](oncRPCServerSetLimits.3.md). Read the limits,
change the fields of interest and write them back, so that limits added in
later versions keep their values.

Nothing happens if either argument is `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void print_limits(const oncRPCServer *server)
{
    oncRPCServerLimits lim;

    oncRPCServerGetLimits(server, &lim);
    printf("up to %zu connections, %zu requests at once, idle timeout %u ms\n",
           lim.maxConnections, lim.maxRequests, (unsigned)lim.idleTimeoutMs);
}
```

## SEE ALSO

[oncRPCServerSetLimits(3)](oncRPCServerSetLimits.3.md),
[oncRPCServerGetStats(3)](oncRPCServerGetStats.3.md)
