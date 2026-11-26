# oncRPCXdrData(3)

## NAME

oncRPCXdrData - the buffer of a stream

## SYNOPSIS

```c
#include <xdr.h>

const uint8_t *oncRPCXdrData(const oncRPCXdr *xdrs);
```

## DESCRIPTION

`oncRPCXdrData()` returns the start of the stream's buffer: for encoding,
the bytes produced so far (their number is given by
[oncRPCXdrLength(3)](oncRPCXdrLength.3.md)); for decoding, the input.

For a dynamic encoding stream the buffer may move as it grows, so fetch
the pointer only after the last value is encoded, and do not use it after
[oncRPCXdrDestroy(3)](oncRPCXdrDestroy.3.md) or
[oncRPCXdrDetach(3)](oncRPCXdrDetach.3.md).

## RETURN VALUE

The buffer, or `NULL` if the stream has none.

## EXAMPLE

```c
#include <stdio.h>
#include <xdr.h>

void hexdump(const oncRPCXdr *xdrs)
{
    const uint8_t *p = oncRPCXdrData(xdrs);
    size_t i, n = oncRPCXdrLength(xdrs);

    for (i = 0; i < n; i++)
        printf("%02x%s", p[i], (i % 4 == 3) ? " " : "");
    printf("\n");
}
```

## SEE ALSO

[oncRPCXdrLength(3)](oncRPCXdrLength.3.md),
[oncRPCXdrDetach(3)](oncRPCXdrDetach.3.md)
