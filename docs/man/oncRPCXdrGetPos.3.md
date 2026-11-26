# oncRPCXdrGetPos(3)

## NAME

oncRPCXdrGetPos - current offset in a stream

## SYNOPSIS

```c
#include <xdr.h>

size_t oncRPCXdrGetPos(const oncRPCXdr *xdrs);
```

## DESCRIPTION

`oncRPCXdrGetPos()` returns how many bytes from the start of the buffer
the stream has encoded or decoded so far. Together with
[oncRPCXdrSetPos(3)](oncRPCXdrSetPos.3.md) it lets a routine remember a
place and come back to it, for example to fill in a length once the data
after it is known.

## RETURN VALUE

The offset in bytes.

## EXAMPLE

Encode a count first, and fill it in once the items are written:

```c
#include <xdr.h>

int encode_nonzero(oncRPCXdr *xdrs, const uint32_t *v, uint32_t n)
{
    size_t countPos = oncRPCXdrGetPos(xdrs), endPos;
    uint32_t count = 0, i;

    if (!oncRPCXdrUint32(xdrs, &count))          /* placeholder */
        return 0;
    for (i = 0; i < n; i++) {
        uint32_t x = v[i];
        if (x != 0) {
            if (!oncRPCXdrUint32(xdrs, &x))
                return 0;
            count++;
        }
    }
    endPos = oncRPCXdrGetPos(xdrs);
    return oncRPCXdrSetPos(xdrs, countPos) && oncRPCXdrUint32(xdrs, &count) &&
           oncRPCXdrSetPos(xdrs, endPos);
}
```

## SEE ALSO

[oncRPCXdrSetPos(3)](oncRPCXdrSetPos.3.md),
[oncRPCXdrLength(3)](oncRPCXdrLength.3.md)
