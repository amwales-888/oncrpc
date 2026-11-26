# oncRPCSvcErrDecode(3)

## NAME

oncRPCSvcErrDecode - reply that the arguments could not be decoded

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCSvcErrDecode(oncRPCSvcReq *req);
```

## DESCRIPTION

`oncRPCSvcErrDecode()` answers `req` with GARBAGE_ARGS, meaning its
arguments could not be decoded, and finishes the request. The client's call
fails with `ONCRPC_ERR_CANTDECODEARGS`.

Use it when [oncRPCSvcDecodeArgs(3)](oncRPCSvcDecodeArgs.3.md) returns
`NULL`, or when the arguments decode but make no sense for the procedure.

Once finished, a request may be freed at once: do not use `req` again. A
`NULL` or already finished request is ignored.

## EXAMPLE

```c
#include <oncrpc.h>

static int xdr_u32(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrUint32(xdrs, (uint32_t *)obj);
}

void percent_proc(oncRPCSvcReq *req)
{
    uint32_t *pct = oncRPCSvcDecodeArgs(req, xdr_u32, sizeof(uint32_t));

    if (pct == NULL || *pct > 100) {   /* malformed, or out of range */
        oncRPCSvcErrDecode(req);
        return;
    }
    oncRPCSvcReply(req, NULL, NULL);
}
```

## SEE ALSO

[oncRPCSvcDecodeArgs(3)](oncRPCSvcDecodeArgs.3.md),
[oncRPCSvcErrSystem(3)](oncRPCSvcErrSystem.3.md)
