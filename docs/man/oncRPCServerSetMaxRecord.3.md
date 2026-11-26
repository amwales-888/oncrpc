# oncRPCServerSetMaxRecord(3)

## NAME

oncRPCServerSetMaxRecord - largest request a server accepts

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCServerSetMaxRecord(oncRPCServer *srv, size_t bytes);
```

## DESCRIPTION

`oncRPCServerSetMaxRecord()` sets the largest request record, in bytes,
the server accepts over TCP and local sockets; a connection that announces
a larger one is closed. It is a shortcut for changing the `maxRecord`
field of [oncRPCServerSetLimits(3)](oncRPCServerSetLimits.3.md). The
default is `ONCRPC_DEFAULT_MAX_RECORD` (16 MiB). A value of 0 is ignored,
as is `NULL`.

UDP requests are limited by the size of a datagram instead.

## EXAMPLE

```c
#include <oncrpc.h>

void accept_uploads(oncRPCServer *server)
{
    oncRPCServerSetMaxRecord(server, (size_t)64 * 1024 * 1024);   /* 64 MiB */
}
```

## SEE ALSO

[oncRPCServerSetLimits(3)](oncRPCServerSetLimits.3.md),
[oncRPCClientSetMaxRecord(3)](oncRPCClientSetMaxRecord.3.md)
