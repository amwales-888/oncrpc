# oncRPCXdrInitFree(3)

## NAME

oncRPCXdrInitFree - make a stream that frees decoded data

## SYNOPSIS

```c
#include <xdr.h>

void oncRPCXdrInitFree(oncRPCXdr *xdrs, const oncRPCAllocator *a);
```

## DESCRIPTION

`oncRPCXdrInitFree()` prepares `xdrs` in free mode (`ONCRPC_XDR_FREE`).
Running an XDR routine on such a stream releases, through `a`, every block
that decoding the same object allocated, and resets the pointers to
`NULL`.

This is the third mode every XDR routine supports, and the reason one
routine is enough to describe a type. Most code calls
[oncRPCXdrFree(3)](oncRPCXdrFree.3.md), which sets up the stream and runs
the routine in one step; `oncRPCXdrInitFree()` is for freeing several
objects, or parts of one, with a single stream.

## EXAMPLE

Release the decoded contents of two separate fields:

```c
#include <xdr.h>

struct pair {
    char    *name;
    uint8_t *blob;
    uint32_t bloblen;
};

void free_pair(const oncRPCAllocator *a, struct pair *p)
{
    oncRPCXdr xdrs;

    oncRPCXdrInitFree(&xdrs, a);
    oncRPCXdrString(&xdrs, &p->name, ONCRPC_XDR_UNBOUNDED);          /* p->name = NULL */
    oncRPCXdrBytes(&xdrs, &p->blob, &p->bloblen, ONCRPC_XDR_UNBOUNDED);
}
```

## SEE ALSO

[oncRPCXdrFree(3)](oncRPCXdrFree.3.md),
[oncRPCXdrInitDecode(3)](oncRPCXdrInitDecode.3.md)
