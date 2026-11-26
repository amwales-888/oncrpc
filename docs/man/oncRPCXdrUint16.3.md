# oncRPCXdrUint16(3)

## NAME

oncRPCXdrUint16 - XDR routine for an unsigned short

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrUint16(oncRPCXdr *xdrs, uint16_t *v);
```

## DESCRIPTION

`oncRPCXdrUint16()` encodes or decodes a `uint16_t` as a full four-byte
XDR `unsigned int`; decoding fails if the received value is above 65535.
The `.x` type `unsigned short` maps to `uint16_t` and this routine.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short or
out of range, or the stream has already failed.

## EXAMPLE

```c
#include <xdr.h>

struct endpoint {
    char    *host;
    uint16_t port;
};

int xdr_endpoint(oncRPCXdr *xdrs, void *obj)
{
    struct endpoint *e = obj;
    return oncRPCXdrString(xdrs, &e->host, 255) && oncRPCXdrUint16(xdrs, &e->port);
}
```

## SEE ALSO

[oncRPCXdrInt16(3)](oncRPCXdrInt16.3.md),
[oncRPCXdrUint32(3)](oncRPCXdrUint32.3.md)
