# oncRPCServerAllocator(3)

## NAME

oncRPCServerAllocator - allocator of a server

## SYNOPSIS

```c
#include <oncrpc.h>

const oncRPCAllocator *oncRPCServerAllocator(const oncRPCServer *srv);
```

## DESCRIPTION

`oncRPCServerAllocator()` returns the allocator given to
[oncRPCServerCreate(3)](oncRPCServerCreate.3.md). Dispatch routines reach
it through `req->server`, for memory that should come from the same
place as the library's.

## RETURN VALUE

The allocator, or `NULL` if `srv` is `NULL`.

## EXAMPLE

Build a result list from the server's allocator and free it after the
reply has been encoded:

```c
#include <oncrpc.h>

static int xdr_u32(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrUint32(xdrs, (uint32_t *)obj);
}

struct u32list { uint32_t *val; uint32_t len; };

static int xdr_u32list(oncRPCXdr *xdrs, void *obj)
{
    struct u32list *l = obj;
    return oncRPCXdrArray(xdrs, (void **)&l->val, &l->len, ONCRPC_XDR_UNBOUNDED,
                          sizeof(uint32_t), xdr_u32);
}

void reply_first_squares(oncRPCSvcReq *req, uint32_t n)
{
    const oncRPCAllocator *a = oncRPCServerAllocator(req->server);
    struct u32list l;
    uint32_t i;

    l.len = n;
    l.val = oncRPCAllocArray(a, n, sizeof(uint32_t));
    if (l.val == NULL) {
        oncRPCSvcErrSystem(req);
        return;
    }
    for (i = 0; i < n; i++)
        l.val[i] = i * i;
    oncRPCSvcReply(req, xdr_u32list, &l);   /* encoded now; not retained */
    oncRPCFree(a, l.val);
}
```

## SEE ALSO

[oncRPCClientAllocator(3)](oncRPCClientAllocator.3.md),
[oncRPCAlloc(3)](oncRPCAlloc.3.md)
