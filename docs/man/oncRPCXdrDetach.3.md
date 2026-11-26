# oncRPCXdrDetach(3)

## NAME

oncRPCXdrDetach - take the buffer of a dynamic encoding stream

## SYNOPSIS

```c
#include <xdr.h>

uint8_t *oncRPCXdrDetach(oncRPCXdr *xdrs, size_t *len);
```

## DESCRIPTION

`oncRPCXdrDetach()` hands the buffer of a dynamic encoding stream (see
[oncRPCXdrInitEncodeDynamic(3)](oncRPCXdrInitEncodeDynamic.3.md)) to the
caller and stores the number of bytes encoded in `*len` (if `len` is not
`NULL`). The stream is left empty; destroying it afterwards is harmless.

The caller then owns the buffer and must release it with
[oncRPCFree(3)](oncRPCFree.3.md), using the stream's allocator.

## RETURN VALUE

The buffer, or `NULL` if the stream is not a dynamic encoding stream (or
its buffer was already taken).

## EXAMPLE

```c
#include <xdr.h>

/* Encode a value into a buffer the caller keeps. */
uint8_t *encode_u64(const oncRPCAllocator *a, uint64_t v, size_t *len)
{
    oncRPCXdr xdrs;

    if (!oncRPCXdrInitEncodeDynamic(&xdrs, a, 8))
        return NULL;
    if (!oncRPCXdrUint64(&xdrs, &v)) {
        oncRPCXdrDestroy(&xdrs);
        return NULL;
    }
    return oncRPCXdrDetach(&xdrs, len);
}
```

## SEE ALSO

[oncRPCXdrInitEncodeDynamic(3)](oncRPCXdrInitEncodeDynamic.3.md),
[oncRPCXdrDestroy(3)](oncRPCXdrDestroy.3.md),
[oncRPCFree(3)](oncRPCFree.3.md)
