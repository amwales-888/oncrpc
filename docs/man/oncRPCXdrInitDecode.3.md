# oncRPCXdrInitDecode(3)

## NAME

oncRPCXdrInitDecode - start decoding from a buffer

## SYNOPSIS

```c
#include <xdr.h>

void oncRPCXdrInitDecode(oncRPCXdr *xdrs, const oncRPCAllocator *a,
                         const uint8_t *buf, size_t len);
```

## DESCRIPTION

`oncRPCXdrInitDecode()` prepares `xdrs` to decode values from the `len`
bytes at `buf`. The bytes are not copied and must stay valid while the
stream is used; they are never modified.

Decoding variable-size data (strings, variable-length opaque data and
arrays, optional data) allocates memory from `a`. When only fixed-size
values are decoded, `a` may be `NULL`.

Memory allocated by a decode belongs to the decoded object. Release it
with [oncRPCXdrFree(3)](oncRPCXdrFree.3.md), using the same XDR routine and
the same allocator.

Decoding checks every length and bound before allocating: a length larger
than the declared maximum, or than the bytes that remain, fails instead of
allocating.

## EXAMPLE

```c
#include <stdio.h>
#include <xdr.h>

int print_message(const oncRPCAllocator *a, const uint8_t *buf, size_t len)
{
    oncRPCXdr xdrs;
    uint32_t id;
    char *text = NULL;

    oncRPCXdrInitDecode(&xdrs, a, buf, len);
    if (!oncRPCXdrUint32(&xdrs, &id) || !oncRPCXdrString(&xdrs, &text, 1024))
        return -1;
    printf("%u: %s\n", (unsigned)id, text);
    oncRPCFree(a, text);
    return 0;
}
```

## SEE ALSO

[oncRPCXdrInitEncode(3)](oncRPCXdrInitEncode.3.md),
[oncRPCXdrFree(3)](oncRPCXdrFree.3.md),
[oncRPCXdrRemaining(3)](oncRPCXdrRemaining.3.md),
[oncRPCXdrGetPos(3)](oncRPCXdrGetPos.3.md)
