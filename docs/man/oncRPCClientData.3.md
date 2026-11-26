# oncRPCClientData(3)

## NAME

oncRPCClientData - application pointer of a client

## SYNOPSIS

```c
#include <oncrpc.h>

void *oncRPCClientData(const oncRPCClient *clnt);
```

## DESCRIPTION

`oncRPCClientData()` returns the `data` pointer given to
[oncRPCClientCreate(3)](oncRPCClientCreate.3.md). It lets every callback
of a client reach the application's state for it, whatever `data` the
individual calls were given.

## RETURN VALUE

The pointer, or `NULL` if `clnt` is `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

struct connection_stats {
    const char *name;
    unsigned    failures;
};

void count_failures(oncRPCClient *client, oncRPCStat status, void *res, void *data)
{
    struct connection_stats *st = oncRPCClientData(client);
    (void)res;
    (void)data;
    if (status != ONCRPC_OK && ++st->failures == 10)
        fprintf(stderr, "%s: 10 failed calls\n", st->name);
}
```

## SEE ALSO

[oncRPCClientCreate(3)](oncRPCClientCreate.3.md),
[oncRPCClientAllocator(3)](oncRPCClientAllocator.3.md)
