# oncRPCRealloc(3)

## NAME

oncRPCRealloc - resize a block through an allocator

## SYNOPSIS

```c
#include <xdr.h>

void *oncRPCRealloc(const oncRPCAllocator *a, void *ptr, size_t size);
```

## DESCRIPTION

`oncRPCRealloc()` calls the allocator's `realloc` function to resize the
block `ptr` to `size` bytes, or to allocate a new one when `ptr` is
`NULL`. A `size` of 0 is passed on as 1. Unlike
[oncRPCAlloc(3)](oncRPCAlloc.3.md), new memory is not zeroed.

## RETURN VALUE

The new block, which may have moved, or `NULL` on failure. On failure
`ptr` is untouched and still has to be freed.

## EXAMPLE

Grow a buffer, keeping the old one if that fails:

```c
#include <oncrpc.h>

int grow(const oncRPCAllocator *a, char **buf, size_t *cap)
{
    size_t want = *cap ? *cap * 2 : 64;
    char *p = oncRPCRealloc(a, *buf, want);

    if (p == NULL)
        return -1;   /* *buf is still valid */
    *buf = p;
    *cap = want;
    return 0;
}
```

## SEE ALSO

[oncRPCAlloc(3)](oncRPCAlloc.3.md),
[oncRPCFree(3)](oncRPCFree.3.md)
