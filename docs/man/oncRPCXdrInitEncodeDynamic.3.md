# oncRPCXdrInitEncodeDynamic(3)

## NAME

oncRPCXdrInitEncodeDynamic - start encoding into a growing buffer

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrInitEncodeDynamic(oncRPCXdr *xdrs, const oncRPCAllocator *a, size_t initial);
```

## DESCRIPTION

`oncRPCXdrInitEncodeDynamic()` prepares `xdrs` to encode into a buffer
that the stream owns and enlarges through the allocator `a` as values are
added. `initial` is the starting capacity in bytes (at least 64 is used).

When encoding is finished, either:

- take the buffer with [oncRPCXdrDetach(3)](oncRPCXdrDetach.3.md), which
  makes the caller responsible for freeing it; or
- read it with [oncRPCXdrData(3)](oncRPCXdrData.3.md) and
  [oncRPCXdrLength(3)](oncRPCXdrLength.3.md), then release it with
  [oncRPCXdrDestroy(3)](oncRPCXdrDestroy.3.md).

## RETURN VALUE

Non-zero on success, 0 if the first buffer could not be allocated. Even
after a failure, calling `oncRPCXdrDestroy()` is safe.

## EXAMPLE

```c
#include <xdr.h>

/* Encode a variable-length list of names; returns the bytes or NULL. */
uint8_t *encode_names(const oncRPCAllocator *a, char **names, uint32_t n, size_t *len)
{
    oncRPCXdr xdrs;
    uint32_t i;

    if (!oncRPCXdrInitEncodeDynamic(&xdrs, a, 256))
        return NULL;
    if (!oncRPCXdrUint32(&xdrs, &n)) {
        oncRPCXdrDestroy(&xdrs);
        return NULL;
    }
    for (i = 0; i < n; i++) {
        if (!oncRPCXdrString(&xdrs, &names[i], 255)) {
            oncRPCXdrDestroy(&xdrs);
            return NULL;
        }
    }
    return oncRPCXdrDetach(&xdrs, len);   /* release with oncRPCFree(a, ...) */
}
```

## SEE ALSO

[oncRPCXdrInitEncode(3)](oncRPCXdrInitEncode.3.md),
[oncRPCXdrDetach(3)](oncRPCXdrDetach.3.md),
[oncRPCXdrDestroy(3)](oncRPCXdrDestroy.3.md)
