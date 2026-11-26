# oncRPCXdrEnum(3)

## NAME

oncRPCXdrEnum - XDR routine for an enumeration value

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrEnum(oncRPCXdr *xdrs, int32_t *v);
```

## DESCRIPTION

`oncRPCXdrEnum()` encodes or decodes an XDR `enum`, which travels as a
signed four-byte integer. It takes an `int32_t`: the size of a C `enum`
varies between compilers, so a hand-written routine copies the value
through an `int32_t`, as below.

The routine does not check that a decoded value is one of the
enumeration's items, and neither does the code `oncrpcgen` generates.
Check the value before relying on it, for example in a `switch` with a
`default` branch.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short, or
the stream has already failed.

## EXAMPLE

```c
#include <xdr.h>

enum color { RED = 0, GREEN = 1, BLUE = 2 };

int xdr_color(oncRPCXdr *xdrs, enum color *c)
{
    int32_t v = (int32_t)*c;

    if (!oncRPCXdrEnum(xdrs, &v))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE) {
        if (v < RED || v > BLUE)
            return 0;   /* not a color */
        *c = (enum color)v;
    }
    return 1;
}
```

## SEE ALSO

[oncRPCXdrInt32(3)](oncRPCXdrInt32.3.md),
[oncrpcgen(1)](oncrpcgen.1.md)
