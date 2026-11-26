# oncRPCXdrLength(3)

## NAME

oncRPCXdrLength - bytes in a stream

## SYNOPSIS

```c
#include <xdr.h>

size_t oncRPCXdrLength(const oncRPCXdr *xdrs);
```

## DESCRIPTION

`oncRPCXdrLength()` returns, for an encoding stream, the number of bytes
produced so far, and for a decoding stream, the size of its input.

XDR pads every item to a multiple of four bytes (`ONCRPC_XDR_UNIT`), so
the length of an encoding is always a multiple of four.

## RETURN VALUE

The length in bytes.

## EXAMPLE

```c
#include <xdr.h>

/* Bytes a string takes on the wire, without keeping the encoding. */
size_t wire_size(const oncRPCAllocator *a, char *s)
{
    oncRPCXdr xdrs;
    size_t n = 0;

    if (oncRPCXdrInitEncodeDynamic(&xdrs, a, 64) &&
        oncRPCXdrString(&xdrs, &s, ONCRPC_XDR_UNBOUNDED))
        n = oncRPCXdrLength(&xdrs);   /* 4 + strlen(s), rounded up to 4 */
    oncRPCXdrDestroy(&xdrs);
    return n;
}
```

## SEE ALSO

[oncRPCXdrData(3)](oncRPCXdrData.3.md),
[oncRPCXdrGetPos(3)](oncRPCXdrGetPos.3.md),
[oncRPCXdrRemaining(3)](oncRPCXdrRemaining.3.md)
