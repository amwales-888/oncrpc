# oncRPCXdrInt32(3)

## NAME

oncRPCXdrInt32 - XDR routine for a signed 32-bit integer

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrInt32(oncRPCXdr *xdrs, int32_t *v);
```

## DESCRIPTION

`oncRPCXdrInt32()` encodes `*v`, or decodes into `*v`, as an XDR `int`:
four bytes, most significant first, two's complement. In free mode it does
nothing. The `.x` types `int` and `long` map to `int32_t` and this routine.

## RETURN VALUE

Non-zero on success; 0 if the encoding buffer is full, fewer than four
bytes remain to decode, or the stream has already failed.

## EXAMPLE

```c
#include <assert.h>
#include <xdr.h>

void round_trip(void)
{
    uint8_t buf[4];
    int32_t out = -2, in = 0;
    oncRPCXdr xdrs;

    oncRPCXdrInitEncode(&xdrs, buf, sizeof(buf));
    oncRPCXdrInt32(&xdrs, &out);                /* ff ff ff fe */
    oncRPCXdrInitDecode(&xdrs, NULL, buf, sizeof(buf));
    oncRPCXdrInt32(&xdrs, &in);
    assert(in == -2);
}
```

## SEE ALSO

[oncRPCXdrUint32(3)](oncRPCXdrUint32.3.md),
[oncRPCXdrInt64(3)](oncRPCXdrInt64.3.md),
[oncRPCXdrInt16(3)](oncRPCXdrInt16.3.md)
