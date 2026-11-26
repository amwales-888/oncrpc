# oncRPCXdrRemaining(3)

## NAME

oncRPCXdrRemaining - bytes left to decode, or room left to encode

## SYNOPSIS

```c
#include <xdr.h>

size_t oncRPCXdrRemaining(const oncRPCXdr *xdrs);
```

## DESCRIPTION

For a decoding stream, `oncRPCXdrRemaining()` returns the number of input
bytes not yet decoded. For an encoding stream with a fixed buffer, it
returns the room left. A dynamic encoding stream has no fixed limit and
gives `SIZE_MAX`; a free-mode stream gives 0.

A decoder can use it to check that a message has been consumed entirely,
which catches peers that send trailing garbage.

## RETURN VALUE

The number of bytes, as described above.

## EXAMPLE

```c
#include <xdr.h>

/* Decode exactly one uint32 and nothing more. */
int decode_exact(const uint8_t *buf, size_t len, uint32_t *v)
{
    oncRPCXdr xdrs;

    oncRPCXdrInitDecode(&xdrs, NULL, buf, len);
    return oncRPCXdrUint32(&xdrs, v) && oncRPCXdrRemaining(&xdrs) == 0;
}
```

## SEE ALSO

[oncRPCXdrLength(3)](oncRPCXdrLength.3.md),
[oncRPCXdrGetPos(3)](oncRPCXdrGetPos.3.md)
