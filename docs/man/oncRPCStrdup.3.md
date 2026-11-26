# oncRPCStrdup(3)

## NAME

oncRPCStrdup - copy a string through an allocator

## SYNOPSIS

```c
#include <xdr.h>

char *oncRPCStrdup(const oncRPCAllocator *a, const char *s);
```

## DESCRIPTION

`oncRPCStrdup()` allocates a copy of the NUL-terminated string `s` from
the allocator `a`, like `strdup()`. Release the copy with
[oncRPCFree(3)](oncRPCFree.3.md).

## RETURN VALUE

The copy, or `NULL` if `s` is `NULL` or the allocator fails.

## EXAMPLE

Keep a string argument after the request that carried it has finished:

```c
#include <oncrpc.h>

char *last_name;   /* kept between requests */

void remember(oncRPCSvcReq *req, const char *name)
{
    const oncRPCAllocator *a = oncRPCServerAllocator(req->server);
    char *copy = oncRPCStrdup(a, name);

    if (copy == NULL) {
        oncRPCSvcErrSystem(req);
        return;
    }
    oncRPCFree(a, last_name);
    last_name = copy;
    oncRPCSvcReply(req, NULL, NULL);
}
```

## SEE ALSO

[oncRPCAlloc(3)](oncRPCAlloc.3.md),
[oncRPCFree(3)](oncRPCFree.3.md)
