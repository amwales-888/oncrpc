# oncRPCXdrArray(3)

## NAME

oncRPCXdrArray - XDR routine for a variable-length array

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrArray(oncRPCXdr *xdrs, void **arr, uint32_t *len, uint32_t max,
                   size_t elsize, oncRPCXdrProc elproc);
```

## DESCRIPTION

`oncRPCXdrArray()` handles XDR `type name<max>`: a four-byte element count
followed by the elements, each encoded by `elproc`. `*arr` points at
`*len` elements of `elsize` bytes each.

- **Encode:** checks `*len` against `max`, sends it, then runs `elproc` on
  each element.
- **Decode:** reads the count and checks it against `max` and against the
  bytes that remain, allocates a zeroed array from the stream's allocator,
  stores it in `*arr` and the count in `*len`, and decodes each element
  into it. An empty array is stored as `NULL`.
- **Free:** runs `elproc` in free mode on each element, then releases the
  array and sets `*arr` to `NULL` and `*len` to 0.

Use `ONCRPC_XDR_UNBOUNDED` as `max` for `type name<>`. For a
fixed-length array, use [oncRPCXdrVector(3)](oncRPCXdrVector.3.md).

Generated code does not call this function; it writes the loop itself with
[oncRPCXdrArrayLen(3)](oncRPCXdrArrayLen.3.md) and
[oncRPCXdrArrayAlloc(3)](oncRPCXdrArrayAlloc.3.md). It is meant for
hand-written routines.

## RETURN VALUE

Non-zero on success; 0 if the count is above `max` or larger than the
remaining input, the allocator fails, an element fails, or the stream has
already failed. If decoding fails part-way, the elements decoded so far are
still in `*arr`; free them with [oncRPCXdrFree(3)](oncRPCXdrFree.3.md).

## EXAMPLE

```c
#include <xdr.h>

struct scores {
    int32_t *val;    /* int scores<100>; */
    uint32_t len;
};

int xdr_int(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrInt32(xdrs, (int32_t *)obj);
}

int xdr_scores(oncRPCXdr *xdrs, void *obj)
{
    struct scores *s = obj;
    return oncRPCXdrArray(xdrs, (void **)&s->val, &s->len, 100, sizeof(int32_t), xdr_int);
}
```

## SEE ALSO

[oncRPCXdrVector(3)](oncRPCXdrVector.3.md),
[oncRPCXdrArrayLen(3)](oncRPCXdrArrayLen.3.md),
[oncRPCXdrFree(3)](oncRPCXdrFree.3.md)
