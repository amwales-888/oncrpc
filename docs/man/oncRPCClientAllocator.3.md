# oncRPCClientAllocator(3)

## NAME

oncRPCClientAllocator - allocator of a client

## SYNOPSIS

```c
#include <oncrpc.h>

const oncRPCAllocator *oncRPCClientAllocator(const oncRPCClient *clnt);
```

## DESCRIPTION

`oncRPCClientAllocator()` returns the allocator given to
[oncRPCClientCreate(3)](oncRPCClientCreate.3.md). Call results are decoded
with it, so it is the allocator to free them with.

## RETURN VALUE

The allocator, or `NULL` if `clnt` is `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

static int xdr_text(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrString(xdrs, (char **)obj, ONCRPC_XDR_UNBOUNDED);
}

char *motd;   /* result of the call, a decoded string */

void on_motd(oncRPCClient *client, oncRPCStat status, void *res, void *data)
{
    (void)data;
    if (status != ONCRPC_OK)
        return;
    printf("%s\n", motd);
    oncRPCXdrFree(oncRPCClientAllocator(client), xdr_text, res);   /* motd = NULL */
}
```

## SEE ALSO

[oncRPCXdrFree(3)](oncRPCXdrFree.3.md),
[oncRPCServerAllocator(3)](oncRPCServerAllocator.3.md)
