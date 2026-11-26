# oncRPCAllocArray(3)

## NAME

oncRPCAllocArray - allocate a zeroed array through an allocator

## SYNOPSIS

```c
#include <xdr.h>

void *oncRPCAllocArray(const oncRPCAllocator *a, size_t count, size_t size);
```

## DESCRIPTION

`oncRPCAllocArray()` allocates room for `count` elements of `size` bytes
each from the allocator `a` and fills it with zeros, like `calloc()`. It
checks that `count * size` does not overflow before allocating.

## RETURN VALUE

A pointer to the zeroed array, or `NULL` if the size overflows or the
allocator fails. An empty array (`count` or `size` 0) still gives a valid
pointer, which must be freed.

## EXAMPLE

```c
#include <oncrpc.h>

uint32_t *make_ids(const oncRPCAllocator *a, size_t n)
{
    uint32_t *ids = oncRPCAllocArray(a, n, sizeof(uint32_t));
    size_t i;

    if (ids == NULL)
        return NULL;
    for (i = 0; i < n; i++)
        ids[i] = (uint32_t)(1000 + i);
    return ids;   /* release with oncRPCFree(a, ids) */
}
```

## SEE ALSO

[oncRPCAlloc(3)](oncRPCAlloc.3.md),
[oncRPCFree(3)](oncRPCFree.3.md)
