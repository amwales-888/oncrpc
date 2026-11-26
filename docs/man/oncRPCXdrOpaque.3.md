# oncRPCXdrOpaque(3)

## NAME

oncRPCXdrOpaque - XDR routine for fixed-length opaque data

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrOpaque(oncRPCXdr *xdrs, uint8_t *data, uint32_t len);
```

## DESCRIPTION

`oncRPCXdrOpaque()` encodes or decodes exactly `len` bytes at `data`,
followed by zero bytes up to the next multiple of four. It is the XDR
`opaque name[len]`. Nothing is allocated: when decoding, `data` must point
at `len` bytes of room. In free mode it does nothing.

For data whose length varies, use
[oncRPCXdrBytes(3)](oncRPCXdrBytes.3.md), which sends the length first.

## RETURN VALUE

Non-zero on success; 0 if `data` is `NULL` while `len` is not 0, the
buffer is full, the input is too short, or the stream has already failed.

## EXAMPLE

A 16-byte identifier (`opaque id[16];` in `.x`):

```c
#include <xdr.h>

struct object_id {
    uint8_t bytes[16];
};

int xdr_object_id(oncRPCXdr *xdrs, void *obj)
{
    struct object_id *id = obj;
    return oncRPCXdrOpaque(xdrs, id->bytes, sizeof(id->bytes));
}
```

## SEE ALSO

[oncRPCXdrBytes(3)](oncRPCXdrBytes.3.md),
[oncRPCXdrString(3)](oncRPCXdrString.3.md)
