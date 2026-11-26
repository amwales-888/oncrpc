# oncRPCXdrUint32(3)

## NAME

oncRPCXdrUint32 - XDR routine for an unsigned 32-bit integer

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrUint32(oncRPCXdr *xdrs, uint32_t *v);
```

## DESCRIPTION

`oncRPCXdrUint32()` encodes `*v`, or decodes into `*v`, as an XDR
`unsigned int`: four bytes, most significant first. In free mode it does
nothing. The `.x` type `unsigned int` (or bare `unsigned`) maps to
`uint32_t` and this routine.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short, or
the stream has already failed.

## EXAMPLE

Read a big-endian length prefix:

```c
#include <xdr.h>

int read_length(const uint8_t *buf, size_t len, uint32_t *n)
{
    oncRPCXdr xdrs;

    oncRPCXdrInitDecode(&xdrs, NULL, buf, len);
    return oncRPCXdrUint32(&xdrs, n);
}
```

## SEE ALSO

[oncRPCXdrInt32(3)](oncRPCXdrInt32.3.md),
[oncRPCXdrUint64(3)](oncRPCXdrUint64.3.md),
[oncRPCXdrEnum(3)](oncRPCXdrEnum.3.md)
