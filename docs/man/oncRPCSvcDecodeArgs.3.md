# oncRPCSvcDecodeArgs(3)

## NAME

oncRPCSvcDecodeArgs - decode a request's arguments

## SYNOPSIS

```c
#include <oncrpc.h>

void *oncRPCSvcDecodeArgs(oncRPCSvcReq *req, oncRPCXdrProc xargs, size_t size);
```

## DESCRIPTION

`oncRPCSvcDecodeArgs()` allocates a zeroed object of `size` bytes and
decodes the arguments of request `req` into it with the XDR routine
`xargs`.

The arguments belong to the request: they stay valid, even if the reply
is sent much later, until the request is finished, and are then released
automatically, strings and arrays included. Do not free them.

It may be called only inside the dispatch routine, and only once per
request, because the request's raw bytes are not kept after the dispatch
routine returns. A server that answers later decodes the arguments first
and keeps the pointer.

Generated dispatch routines call it for you and pass the decoded
arguments to the procedure functions.

## RETURN VALUE

The decoded arguments, or `NULL` if they could not be decoded (the
request is malformed), the allocator failed, `xargs` is `NULL`, or the
function was called outside the dispatch routine or a second time. When
it returns `NULL` for a malformed request, answer with
[oncRPCSvcErrDecode(3)](oncRPCSvcErrDecode.3.md).

## EXAMPLE

```c
#include <oncrpc.h>

struct rename_args {
    char *from;
    char *to;
};

static int xdr_rename_args(oncRPCXdr *xdrs, void *obj)
{
    struct rename_args *a = obj;
    return oncRPCXdrString(xdrs, &a->from, 255) && oncRPCXdrString(xdrs, &a->to, 255);
}

static int xdr_int(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrInt32(xdrs, (int32_t *)obj);
}

int do_rename(const char *from, const char *to);   /* the application's work */

void rename_proc(oncRPCSvcReq *req)
{
    struct rename_args *args = oncRPCSvcDecodeArgs(req, xdr_rename_args, sizeof(*args));
    int32_t result;

    if (args == NULL) {
        oncRPCSvcErrDecode(req);
        return;
    }
    result = do_rename(args->from, args->to);
    oncRPCSvcReply(req, xdr_int, &result);
    /* args, and the strings in it, are released by the library */
}
```

## SEE ALSO

[oncRPCSvcReply(3)](oncRPCSvcReply.3.md),
[oncRPCSvcErrDecode(3)](oncRPCSvcErrDecode.3.md),
[oncRPCServerRegister(3)](oncRPCServerRegister.3.md)
