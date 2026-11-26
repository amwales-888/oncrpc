# oncRPCXdrBytes(3)

## NAME

oncRPCXdrBytes - XDR routine for variable-length opaque data

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrBytes(oncRPCXdr *xdrs, uint8_t **data, uint32_t *len, uint32_t max);
```

## DESCRIPTION

`oncRPCXdrBytes()` handles XDR `opaque name<max>`: a four-byte length
followed by that many bytes, padded to a multiple of four.

- **Encode:** sends `*len` bytes from `*data`. Fails if `*len` is above
  `max`.
- **Decode:** reads the length, checks it against `max` and against the
  bytes that remain, allocates a buffer from the stream's allocator, and
  stores it in `*data` and the length in `*len`.
- **Free:** releases `*data` and sets it to `NULL` and `*len` to 0.

Use `ONCRPC_XDR_UNBOUNDED` as `max` for `opaque name<>`. A bound protects a
decoder from allocating whatever size a peer claims, so give one whenever
the protocol has one.

## RETURN VALUE

Non-zero on success; 0 if the length is above `max`, the input is too
short, the allocator fails, the buffer is full, or the stream has already
failed. A failed decode allocates nothing.

## EXAMPLE

```c
#include <xdr.h>

struct blob {
    uint8_t *data;
    uint32_t len;
};

/* opaque contents<65536>; */
int xdr_blob(oncRPCXdr *xdrs, void *obj)
{
    struct blob *b = obj;
    return oncRPCXdrBytes(xdrs, &b->data, &b->len, 65536);
}
```

Decoded blobs are released with `oncRPCXdrFree(alloc, xdr_blob, &b)`.

## SEE ALSO

[oncRPCXdrOpaque(3)](oncRPCXdrOpaque.3.md),
[oncRPCXdrString(3)](oncRPCXdrString.3.md),
[oncRPCXdrFree(3)](oncRPCXdrFree.3.md)
