# oncRPCXdrInt64(3)

## NAME

oncRPCXdrInt64 - XDR routine for a signed 64-bit integer (hyper)

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrInt64(oncRPCXdr *xdrs, int64_t *v);
```

## DESCRIPTION

`oncRPCXdrInt64()` encodes `*v`, or decodes into `*v`, as an XDR `hyper`:
eight bytes, most significant first, two's complement. In free mode it does
nothing. The `.x` type `hyper` maps to `int64_t` and this routine.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short, or
the stream has already failed.

## EXAMPLE

```c
#include <xdr.h>

/* A file offset in a request: hyper offset; */
int xdr_seek_args(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrInt64(xdrs, (int64_t *)obj);
}
```

## SEE ALSO

[oncRPCXdrUint64(3)](oncRPCXdrUint64.3.md),
[oncRPCXdrInt32(3)](oncRPCXdrInt32.3.md)
