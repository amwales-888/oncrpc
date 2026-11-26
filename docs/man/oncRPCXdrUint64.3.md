# oncRPCXdrUint64(3)

## NAME

oncRPCXdrUint64 - XDR routine for an unsigned 64-bit integer

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrUint64(oncRPCXdr *xdrs, uint64_t *v);
```

## DESCRIPTION

`oncRPCXdrUint64()` encodes `*v`, or decodes into `*v`, as an XDR
`unsigned hyper`: eight bytes, most significant first. In free mode it does
nothing. The `.x` type `unsigned hyper` maps to `uint64_t` and this
routine.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short, or
the stream has already failed.

## EXAMPLE

```c
#include <stdio.h>
#include <xdr.h>

void print_size(const uint8_t *buf, size_t len)
{
    oncRPCXdr xdrs;
    uint64_t bytes;

    oncRPCXdrInitDecode(&xdrs, NULL, buf, len);
    if (oncRPCXdrUint64(&xdrs, &bytes))
        printf("%llu bytes\n", (unsigned long long)bytes);
}
```

## SEE ALSO

[oncRPCXdrInt64(3)](oncRPCXdrInt64.3.md),
[oncRPCXdrUint32(3)](oncRPCXdrUint32.3.md)
