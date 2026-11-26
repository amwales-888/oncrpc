# oncRPCXdrDestroy(3)

## NAME

oncRPCXdrDestroy - release a stream's buffer

## SYNOPSIS

```c
#include <xdr.h>

void oncRPCXdrDestroy(oncRPCXdr *xdrs);
```

## DESCRIPTION

`oncRPCXdrDestroy()` releases the buffer of a dynamic encoding stream
(see [oncRPCXdrInitEncodeDynamic(3)](oncRPCXdrInitEncodeDynamic.3.md)) and
empties the stream. On any other kind of stream, and on a dynamic stream
whose buffer was taken with [oncRPCXdrDetach(3)](oncRPCXdrDetach.3.md), it
only empties it. A `NULL` stream is ignored.

Because it is safe on every stream, error paths can call it without
tracking which kind of stream they have.

It does not free data a decode allocated; that belongs to the decoded
object (see [oncRPCXdrFree(3)](oncRPCXdrFree.3.md)).

## EXAMPLE

```c
#include <stdio.h>
#include <xdr.h>

void log_encoded_size(const oncRPCAllocator *a, char *text)
{
    oncRPCXdr xdrs;

    if (oncRPCXdrInitEncodeDynamic(&xdrs, a, 64) &&
        oncRPCXdrString(&xdrs, &text, ONCRPC_XDR_UNBOUNDED))
        printf("%zu bytes on the wire\n", oncRPCXdrLength(&xdrs));
    oncRPCXdrDestroy(&xdrs);
}
```

## SEE ALSO

[oncRPCXdrInitEncodeDynamic(3)](oncRPCXdrInitEncodeDynamic.3.md),
[oncRPCXdrDetach(3)](oncRPCXdrDetach.3.md)
