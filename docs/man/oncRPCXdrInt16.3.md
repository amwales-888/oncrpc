# oncRPCXdrInt16(3)

## NAME

oncRPCXdrInt16 - XDR routine for a short

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrInt16(oncRPCXdr *xdrs, int16_t *v);
```

## DESCRIPTION

`oncRPCXdrInt16()` encodes or decodes an `int16_t` as a full four-byte XDR
`int`; decoding fails if the received value is outside -32768 to 32767.
The `.x` type `short` maps to `int16_t` and this routine.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short or
out of range, or the stream has already failed.

## EXAMPLE

```c
#include <xdr.h>

int xdr_temperature_tenths(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrInt16(xdrs, (int16_t *)obj);   /* -40.0 C travels as -400 */
}
```

## SEE ALSO

[oncRPCXdrUint16(3)](oncRPCXdrUint16.3.md),
[oncRPCXdrInt8(3)](oncRPCXdrInt8.3.md),
[oncRPCXdrInt32(3)](oncRPCXdrInt32.3.md)
