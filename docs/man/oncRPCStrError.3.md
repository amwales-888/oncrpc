# oncRPCStrError(3)

## NAME

oncRPCStrError - describe a status code in words

## SYNOPSIS

```c
#include <oncrpc.h>

const char *oncRPCStrError(oncRPCStat stat);
```

## DESCRIPTION

`oncRPCStrError()` returns a short English description of `stat`, such as
`"timed out"` for `ONCRPC_ERR_TIMEDOUT` or `"success"` for `ONCRPC_OK`.
It is meant for log messages and error reports.

The text is a constant string: do not modify or free it.

## RETURN VALUE

A pointer to a static, NUL-terminated string. A value that is not an
`oncRPCStat` gives `"unknown error"`. The function never returns `NULL`.

## NOTES

For a failed call, more detail is available from
[oncRPCClientLastError(3)](oncRPCClientLastError.3.md): the supported
version range for a version mismatch, the reason for an authentication
error, the libuv error behind a transport failure and, for TLS, a
description of what went wrong.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void on_done(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)client;
    (void)result;
    (void)data;
    if (status != ONCRPC_OK)
        fprintf(stderr, "call failed: %s\n", oncRPCStrError(status));
}
```

## SEE ALSO

[oncRPCClientLastError(3)](oncRPCClientLastError.3.md),
[liboncrpc(3)](liboncrpc.3.md)
