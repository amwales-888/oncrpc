# oncRPCXdrInt8(3)

## NAME

oncRPCXdrInt8 - XDR routine for a signed char

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrInt8(oncRPCXdr *xdrs, int8_t *v);
```

## DESCRIPTION

`oncRPCXdrInt8()` encodes or decodes an `int8_t`. XDR has no type smaller
than four bytes, so the value travels as a full XDR `int`; decoding fails if
the received value is outside -128 to 127. The `.x` type `char` maps to
`int8_t` and this routine.

To send bytes, use opaque data instead
([oncRPCXdrOpaque(3)](oncRPCXdrOpaque.3.md),
[oncRPCXdrBytes(3)](oncRPCXdrBytes.3.md)), which takes one byte per byte
rather than four.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short or
out of range, or the stream has already failed.

## EXAMPLE

```c
#include <xdr.h>

struct offset {
    int8_t dx;
    int8_t dy;
};

int xdr_offset(oncRPCXdr *xdrs, void *obj)
{
    struct offset *o = obj;
    return oncRPCXdrInt8(xdrs, &o->dx) && oncRPCXdrInt8(xdrs, &o->dy);   /* 8 bytes */
}
```

## SEE ALSO

[oncRPCXdrUint8(3)](oncRPCXdrUint8.3.md),
[oncRPCXdrInt16(3)](oncRPCXdrInt16.3.md),
[oncRPCXdrInt32(3)](oncRPCXdrInt32.3.md)
