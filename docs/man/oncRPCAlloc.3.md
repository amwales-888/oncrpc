# oncRPCAlloc(3)

## NAME

oncRPCAlloc - allocate zeroed memory through an allocator

## SYNOPSIS

```c
#include <xdr.h>

void *oncRPCAlloc(const oncRPCAllocator *a, size_t size);
```

## DESCRIPTION

`oncRPCAlloc()` asks the allocator `a` for a block of `size` bytes and
fills it with zeros. A `size` of 0 is treated as 1, so the result can
always be released with [oncRPCFree(3)](oncRPCFree.3.md).

liboncrpc never calls `malloc()`, `realloc()` or `free()` itself. Every
block it needs comes from an `oncRPCAllocator` supplied by the
application:

```c
typedef struct oncRPCAllocator {
    void *(*realloc)(void *ctx, void *ptr, size_t size);
    void  (*free)(void *ctx, void *ptr);
    void   *ctx;
} oncRPCAllocator;
```

`realloc` behaves like the C function of that name (`ptr` may be `NULL`;
`size` is never 0; it returns `NULL` on failure and leaves `ptr` valid).
`free` releases a block and is never called with `NULL`. `ctx` is passed to
both unchanged, for pools, arenas or accounting. The structure must stay
valid and unchanged for as long as anything created with it exists, which
is why it is usually a `static const` object.

Applications use `oncRPCAlloc()` for memory the library will later free,
and to keep one allocator for everything.

## RETURN VALUE

A pointer to the zeroed block, or `NULL` if the allocator failed or `a`
is `NULL` or has no `realloc` function.

## EXAMPLE

An allocator built on the C library, and a zeroed structure from it:

```c
#include <stdlib.h>
#include <oncrpc.h>

static void *my_realloc(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    return realloc(ptr, size);
}

static void my_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

const oncRPCAllocator my_alloc = { my_realloc, my_free, NULL };

struct job {
    oncRPCSvcReq *req;
    int           attempts;
};

struct job *new_job(oncRPCSvcReq *req)
{
    struct job *j = oncRPCAlloc(&my_alloc, sizeof(*j));   /* attempts == 0 */
    if (j != NULL)
        j->req = req;
    return j;
}
```

## SEE ALSO

[oncRPCAllocArray(3)](oncRPCAllocArray.3.md),
[oncRPCRealloc(3)](oncRPCRealloc.3.md),
[oncRPCFree(3)](oncRPCFree.3.md),
[oncRPCStrdup(3)](oncRPCStrdup.3.md)
