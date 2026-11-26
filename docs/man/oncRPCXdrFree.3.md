# oncRPCXdrFree(3)

## NAME

oncRPCXdrFree - release everything a decode allocated

## SYNOPSIS

```c
#include <xdr.h>

void oncRPCXdrFree(const oncRPCAllocator *a, oncRPCXdrProc proc, void *obj);
```

## DESCRIPTION

`oncRPCXdrFree()` runs the XDR routine `proc` on `obj` in free mode, which
releases, through `a`, every string, array, opaque buffer and optional
item that decoding `obj` allocated, and sets their pointers to `NULL`. The
object itself is not freed: it is usually a variable or a structure member
that the caller owns.

Use the same routine and allocator as the decode. For a client call that
is the result routine and the client's allocator; for generated types,
`oncrpcgen` also writes a typed wrapper for each type,
`oncRPCXdrFree<Type>(alloc, obj)`.

Freeing an object whose pointers are all `NULL` does nothing, so calling
it on a zero-initialised object, or twice, is safe. A `NULL` `proc` or
`obj` is ignored.

`oncRPCXdrProc` is the type of every XDR routine passed as a value:

```c
typedef int (*oncRPCXdrProc)(oncRPCXdr *xdrs, void *obj);
```

A routine with a more specific pointer type, such as `oncRPCXdrString`'s
wrappers or the generated `oncRPCXdr<Type>` functions, is cast to it.

## EXAMPLE

Release a client call's results once they have been used:

```c
#include <stdio.h>
#include <oncrpc.h>

struct names {
    uint32_t len;
    char   **val;
};

int xdr_name(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrString(xdrs, (char **)obj, 255);
}

int xdr_names(oncRPCXdr *xdrs, void *obj)
{
    struct names *n = obj;
    return oncRPCXdrArray(xdrs, (void **)&n->val, &n->len, 100, sizeof(char *), xdr_name);
}

struct names result;   /* zero-initialised; filled in by the call */

void on_list(oncRPCClient *client, oncRPCStat status, void *res, void *data)
{
    uint32_t i;
    (void)data;
    if (status != ONCRPC_OK)
        return;
    for (i = 0; i < result.len; i++)
        printf("%s\n", result.val[i]);
    oncRPCXdrFree(oncRPCClientAllocator(client), xdr_names, res);   /* res == &result */
}
```

## SEE ALSO

[oncRPCXdrInitFree(3)](oncRPCXdrInitFree.3.md),
[oncRPCClientCall(3)](oncRPCClientCall.3.md),
[oncRPCFree(3)](oncRPCFree.3.md)
