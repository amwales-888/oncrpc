# oncRPCClientSetMaxRecord(3)

## NAME

oncRPCClientSetMaxRecord - largest reply a TCP client accepts

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCClientSetMaxRecord(oncRPCClient *clnt, size_t bytes);
```

## DESCRIPTION

Over TCP (and local sockets) each reply travels as a record, possibly in
several fragments. `oncRPCClientSetMaxRecord()` sets the largest record,
in bytes, the client accepts. A larger reply is treated as a broken
connection: it is closed and the calls sent on it fail. The default is
`ONCRPC_DEFAULT_MAX_RECORD` (16 MiB). A value of 0 is ignored, as is
`NULL`.

The limit protects the client from a server, or something pretending to
be one, that announces an enormous reply. Raise it for procedures that
legitimately return more.

## EXAMPLE

```c
#include <oncrpc.h>

void allow_big_downloads(oncRPCClient *client)
{
    oncRPCClientSetMaxRecord(client, (size_t)256 * 1024 * 1024);   /* 256 MiB */
}
```

## SEE ALSO

[oncRPCServerSetMaxRecord(3)](oncRPCServerSetMaxRecord.3.md),
[oncRPCClientCreate(3)](oncRPCClientCreate.3.md)
