# oncRPCXdrSetPos(3)

## NAME

oncRPCXdrSetPos - move to an offset in a stream

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrSetPos(oncRPCXdr *xdrs, size_t pos);
```

## DESCRIPTION

`oncRPCXdrSetPos()` moves the stream to byte offset `pos`. The offset must
not be beyond the data: for decoding, the length of the input; for
encoding, the bytes produced so far. Encoding at an earlier position
overwrites what is there without discarding what follows, so the stream
can patch a value and then move back to the end.

A decoder can also use it to skip ahead, or to decode the same bytes
twice.

## RETURN VALUE

Non-zero on success, 0 if `pos` is beyond the data.

## EXAMPLE

Look at a message's type before decoding the rest:

```c
#include <xdr.h>

int message_type(oncRPCXdr *xdrs, uint32_t *type)
{
    size_t start = oncRPCXdrGetPos(xdrs);

    if (!oncRPCXdrUint32(xdrs, type))
        return 0;
    return oncRPCXdrSetPos(xdrs, start);   /* the caller decodes it all */
}
```

## SEE ALSO

[oncRPCXdrGetPos(3)](oncRPCXdrGetPos.3.md),
[oncRPCXdrReset(3)](oncRPCXdrReset.3.md)
