# oncRPCXdrVoid(3)

## NAME

oncRPCXdrVoid - XDR routine for no data

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrVoid(oncRPCXdr *xdrs, void *unused);
```

## DESCRIPTION

`oncRPCXdrVoid()` encodes and decodes nothing and always succeeds. It
stands for XDR `void`: a procedure without arguments or results, or a
union arm that carries no data.

Where the library accepts an XDR routine for a call's arguments or results,
`NULL` means the same thing, so `oncRPCXdrVoid` is mostly useful in tables
of routines and in hand-written union code.

## RETURN VALUE

Always 1.

## EXAMPLE

A table of result routines indexed by procedure number, with void slots:

```c
#include <xdr.h>

int xdr_counter(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrUint32(xdrs, (uint32_t *)obj);
}

const oncRPCXdrProc result_routines[] = {
    oncRPCXdrVoid,   /* 0: NULL procedure */
    xdr_counter,     /* 1: GET            */
    oncRPCXdrVoid    /* 2: RESET          */
};
```

## SEE ALSO

[oncRPCClientCall(3)](oncRPCClientCall.3.md),
[oncRPCSvcReply(3)](oncRPCSvcReply.3.md)
