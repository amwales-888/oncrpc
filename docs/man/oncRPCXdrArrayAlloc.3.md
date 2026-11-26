# oncRPCXdrArrayAlloc(3)

## NAME

oncRPCXdrArrayAlloc - allocate element storage while decoding

## SYNOPSIS

```c
#include <xdr.h>

void *oncRPCXdrArrayAlloc(oncRPCXdr *xdrs, uint32_t count, size_t elsize);
```

## DESCRIPTION

`oncRPCXdrArrayAlloc()` allocates a zeroed array of `count` elements of
`elsize` bytes from the stream's allocator, checking the size for
overflow. If the allocation fails it also marks the stream as failed, so
the failure is not lost in a long sequence of calls.

Generated code uses it for arrays and optional data while decoding.
Release the storage in free mode with
[oncRPCXdrMemFree(3)](oncRPCXdrMemFree.3.md).

## RETURN VALUE

The zeroed storage, or `NULL` on failure.

## EXAMPLE

See [oncRPCXdrArrayLen(3)](oncRPCXdrArrayLen.3.md) for an array, and this
for a single optional value:

```c
#include <xdr.h>

int xdr_maybe_double(oncRPCXdr *xdrs, double **pp)
{
    oncRPCBool present = *pp != NULL;

    if (xdrs->op == ONCRPC_XDR_FREE) {
        oncRPCXdrMemFree(xdrs, *pp);
        *pp = NULL;
        return 1;
    }
    if (!oncRPCXdrBool(xdrs, &present))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE) {
        *pp = NULL;
        if (present && (*pp = oncRPCXdrArrayAlloc(xdrs, 1, sizeof(double))) == NULL)
            return 0;
    }
    return !present || oncRPCXdrDouble(xdrs, *pp);
}
```

## SEE ALSO

[oncRPCXdrArrayLen(3)](oncRPCXdrArrayLen.3.md),
[oncRPCXdrMemFree(3)](oncRPCXdrMemFree.3.md),
[oncRPCAllocArray(3)](oncRPCAllocArray.3.md)
