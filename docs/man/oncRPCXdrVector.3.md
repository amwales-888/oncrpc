# oncRPCXdrVector(3)

## NAME

oncRPCXdrVector - XDR routine for a fixed-length array

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrVector(oncRPCXdr *xdrs, void *arr, uint32_t count, size_t elsize,
                    oncRPCXdrProc elproc);
```

## DESCRIPTION

`oncRPCXdrVector()` handles XDR `type name[count]`: exactly `count`
elements, each encoded by `elproc`, with no length sent. `arr` points at
`count` elements of `elsize` bytes, which the caller provides; nothing is
allocated for the array itself. In free mode each element is freed (for
elements that own memory, such as strings), but not the array.

## RETURN VALUE

Non-zero on success; 0 if an element fails or the stream has already
failed.

## EXAMPLE

```c
#include <xdr.h>

struct matrix {
    double m[3][3];   /* double m[9]; */
};

int xdr_double(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrDouble(xdrs, (double *)obj);
}

int xdr_matrix(oncRPCXdr *xdrs, void *obj)
{
    struct matrix *mx = obj;
    return oncRPCXdrVector(xdrs, &mx->m[0][0], 9, sizeof(double), xdr_double);
}
```

## SEE ALSO

[oncRPCXdrArray(3)](oncRPCXdrArray.3.md),
[oncRPCXdrOpaque(3)](oncRPCXdrOpaque.3.md)
