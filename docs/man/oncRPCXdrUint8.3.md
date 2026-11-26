# oncRPCXdrUint8(3)

## NAME

oncRPCXdrUint8 - XDR routine for an unsigned char

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrUint8(oncRPCXdr *xdrs, uint8_t *v);
```

## DESCRIPTION

`oncRPCXdrUint8()` encodes or decodes a `uint8_t` as a full four-byte XDR
`unsigned int`; decoding fails if the received value is above 255. The
`.x` type `unsigned char` maps to `uint8_t` and this routine.

For byte strings use opaque data
([oncRPCXdrOpaque(3)](oncRPCXdrOpaque.3.md)), not arrays of
`unsigned char`, which take four bytes per element.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short or
out of range, or the stream has already failed.

## EXAMPLE

```c
#include <xdr.h>

struct rgb {
    uint8_t r, g, b;
};

int xdr_rgb(oncRPCXdr *xdrs, void *obj)
{
    struct rgb *c = obj;
    return oncRPCXdrUint8(xdrs, &c->r) && oncRPCXdrUint8(xdrs, &c->g) &&
           oncRPCXdrUint8(xdrs, &c->b);
}
```

## SEE ALSO

[oncRPCXdrInt8(3)](oncRPCXdrInt8.3.md),
[oncRPCXdrUint16(3)](oncRPCXdrUint16.3.md)
