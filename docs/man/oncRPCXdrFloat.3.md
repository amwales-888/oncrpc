# oncRPCXdrFloat(3)

## NAME

oncRPCXdrFloat - XDR routine for a single-precision float

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrFloat(oncRPCXdr *xdrs, float *v);
```

## DESCRIPTION

`oncRPCXdrFloat()` encodes `*v`, or decodes into `*v`, as an XDR `float`:
the four bytes of the IEEE 754 single-precision value, most significant
first. The bits are copied unchanged, so infinities and NaNs travel as
they are. It assumes, as virtually every current platform does, that
`float` is IEEE 754 single precision.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short, or
the stream has already failed.

## EXAMPLE

```c
#include <xdr.h>

struct reading {
    uint32_t sensor;
    float    celsius;
};

int xdr_reading(oncRPCXdr *xdrs, void *obj)
{
    struct reading *r = obj;
    return oncRPCXdrUint32(xdrs, &r->sensor) && oncRPCXdrFloat(xdrs, &r->celsius);
}
```

## SEE ALSO

[oncRPCXdrDouble(3)](oncRPCXdrDouble.3.md)
