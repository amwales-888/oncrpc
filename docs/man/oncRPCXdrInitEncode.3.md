# oncRPCXdrInitEncode(3)

## NAME

oncRPCXdrInitEncode - start encoding into a fixed buffer

## SYNOPSIS

```c
#include <xdr.h>

void oncRPCXdrInitEncode(oncRPCXdr *xdrs, uint8_t *buf, size_t cap);
```

## DESCRIPTION

`oncRPCXdrInitEncode()` prepares the stream `xdrs` to encode values into
the caller's buffer `buf` of `cap` bytes. XDR routines called on the stream
then append their network representation, and fail once the buffer is full.

No allocator is involved and nothing needs releasing afterwards. To encode
something whose size is not known in advance, use
[oncRPCXdrInitEncodeDynamic(3)](oncRPCXdrInitEncodeDynamic.3.md).

An `oncRPCXdr` is an ordinary structure, usually on the stack. Treat its
fields as read-only and use the accessor functions:
[oncRPCXdrLength(3)](oncRPCXdrLength.3.md) for the bytes produced,
[oncRPCXdrData(3)](oncRPCXdrData.3.md) for the buffer.

Every XDR routine returns non-zero on success and 0 on failure. Once a
routine has failed the stream's `error` field is set, so a sequence of
calls can be checked at the end.

## EXAMPLE

```c
#include <stdio.h>
#include <xdr.h>

void encode_point(int32_t x, int32_t y)
{
    uint8_t buf[8];
    oncRPCXdr xdrs;

    oncRPCXdrInitEncode(&xdrs, buf, sizeof(buf));
    if (!oncRPCXdrInt32(&xdrs, &x) || !oncRPCXdrInt32(&xdrs, &y)) {
        fprintf(stderr, "buffer too small\n");
        return;
    }
    printf("encoded %zu bytes\n", oncRPCXdrLength(&xdrs));   /* 8 */
}
```

## SEE ALSO

[oncRPCXdrInitEncodeDynamic(3)](oncRPCXdrInitEncodeDynamic.3.md),
[oncRPCXdrInitDecode(3)](oncRPCXdrInitDecode.3.md),
[oncRPCXdrReset(3)](oncRPCXdrReset.3.md),
[oncRPCXdrRemaining(3)](oncRPCXdrRemaining.3.md)
