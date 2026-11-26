# oncRPCFree(3)

## NAME

oncRPCFree - release a block through an allocator

## SYNOPSIS

```c
#include <xdr.h>

void oncRPCFree(const oncRPCAllocator *a, void *ptr);
```

## DESCRIPTION

`oncRPCFree()` gives `ptr` back to the allocator `a`. It must be the
allocator the block came from. A `NULL` pointer is ignored, as with the C
`free()`.

Use it for:

- blocks from [oncRPCAlloc(3)](oncRPCAlloc.3.md) and the other
  allocation helpers;
- a string returned by a generated client stub whose result type is
  `string` (the stub's result is a `char *`);
- a buffer taken with [oncRPCXdrDetach(3)](oncRPCXdrDetach.3.md) or
  produced by [oncRPCXSpecGenerate(3)](oncRPCXSpecGenerate.3.md).

Structures that contain decoded data (strings, arrays, optional data) are
released with [oncRPCXdrFree(3)](oncRPCXdrFree.3.md) instead, which walks
the structure and frees every part.

## EXAMPLE

A client callback for a procedure that returns a string:

```c
#include <stdio.h>
#include <oncrpc.h>

char *greeting;   /* the call's result */

void on_hello(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    if (status == ONCRPC_OK) {
        printf("%s\n", greeting);
        oncRPCFree(oncRPCClientAllocator(client), greeting);
        greeting = NULL;
    }
}
```

## SEE ALSO

[oncRPCAlloc(3)](oncRPCAlloc.3.md),
[oncRPCXdrFree(3)](oncRPCXdrFree.3.md)
