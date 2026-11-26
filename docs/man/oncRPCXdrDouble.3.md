# oncRPCXdrDouble(3)

## NAME

oncRPCXdrDouble - XDR routine for a double-precision float

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrDouble(oncRPCXdr *xdrs, double *v);
```

## DESCRIPTION

`oncRPCXdrDouble()` encodes `*v`, or decodes into `*v`, as an XDR
`double`: the eight bytes of the IEEE 754 double-precision value, most
significant first. The bits are copied unchanged.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short, or
the stream has already failed.

## EXAMPLE

```c
#include <xdr.h>

struct position {
    double latitude;
    double longitude;
};

int xdr_position(oncRPCXdr *xdrs, void *obj)
{
    struct position *p = obj;
    return oncRPCXdrDouble(xdrs, &p->latitude) && oncRPCXdrDouble(xdrs, &p->longitude);
}
```

## SEE ALSO

[oncRPCXdrFloat(3)](oncRPCXdrFloat.3.md)
