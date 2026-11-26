# oncRPCXdrBool(3)

## NAME

oncRPCXdrBool - XDR routine for a boolean

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrBool(oncRPCXdr *xdrs, oncRPCBool *v);
```

## DESCRIPTION

`oncRPCXdrBool()` encodes or decodes an XDR `bool`, which travels as a
four-byte integer that is 0 (`ONCRPC_FALSE`) or 1 (`ONCRPC_TRUE`).
`oncRPCBool` is an `int32_t`.

When encoding, any non-zero value is sent as 1. When decoding, a value
other than 0 or 1 is an error.

## RETURN VALUE

Non-zero on success; 0 if the buffer is full, the input is too short or
holds a value other than 0 or 1, or the stream has already failed.

## EXAMPLE

```c
#include <xdr.h>

struct flags {
    oncRPCBool readonly;
    oncRPCBool hidden;
};

int xdr_flags(oncRPCXdr *xdrs, void *obj)
{
    struct flags *f = obj;
    return oncRPCXdrBool(xdrs, &f->readonly) && oncRPCXdrBool(xdrs, &f->hidden);
}
```

## SEE ALSO

[oncRPCXdrEnum(3)](oncRPCXdrEnum.3.md),
[oncRPCXdrPointer(3)](oncRPCXdrPointer.3.md)
