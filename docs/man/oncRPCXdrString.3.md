# oncRPCXdrString(3)

## NAME

oncRPCXdrString - XDR routine for a string

## SYNOPSIS

```c
#include <xdr.h>

int oncRPCXdrString(oncRPCXdr *xdrs, char **s, uint32_t max);
```

## DESCRIPTION

`oncRPCXdrString()` handles XDR `string name<max>`: a four-byte length
followed by the characters, without a terminating NUL, padded to a multiple
of four.

- **Encode:** sends the NUL-terminated string `*s`. A `NULL` pointer is
  sent as the empty string. Fails if the string is longer than `max`.
- **Decode:** reads the length, checks it against `max` and against the
  bytes that remain, allocates `length + 1` bytes from the stream's
  allocator and stores the NUL-terminated string in `*s`.
- **Free:** releases `*s` and sets it to `NULL`.

Use `ONCRPC_XDR_UNBOUNDED` as `max` for `string name<>`. XDR strings may
contain any bytes except that C code sees a NUL as the end, so text with
embedded NULs should use [oncRPCXdrBytes(3)](oncRPCXdrBytes.3.md).

## RETURN VALUE

Non-zero on success; 0 if the string is longer than `max`, the input is
too short, the allocator fails, the buffer is full, or the stream has
already failed.

## EXAMPLE

```c
#include <xdr.h>

struct user {
    char    *name;    /* string name<32>; */
    uint32_t uid;
};

int xdr_user(oncRPCXdr *xdrs, void *obj)
{
    struct user *u = obj;
    return oncRPCXdrString(xdrs, &u->name, 32) && oncRPCXdrUint32(xdrs, &u->uid);
}
```

## SEE ALSO

[oncRPCXdrBytes(3)](oncRPCXdrBytes.3.md),
[oncRPCXdrFree(3)](oncRPCXdrFree.3.md),
[oncRPCStrdup(3)](oncRPCStrdup.3.md)
