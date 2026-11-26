# oncRPCSvcErrNoProc(3)

## NAME

oncRPCSvcErrNoProc - reply that the procedure does not exist

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCSvcErrNoProc(oncRPCSvcReq *req);
```

## DESCRIPTION

`oncRPCSvcErrNoProc()` answers `req` with PROC_UNAVAIL, meaning the
program version has no procedure `req->proc`, and finishes the request.
The client's call fails with `ONCRPC_ERR_PROCUNAVAIL`.

Dispatch routines use it in the `default` branch of their `switch`.
Generated dispatch routines do so too.

Once finished, a request may be freed at once: do not use `req` again. A
`NULL` or already finished request is ignored.

## EXAMPLE

```c
#include <oncrpc.h>

void dispatch(oncRPCSvcReq *req)
{
    switch (req->proc) {
    case 0:
        oncRPCSvcReply(req, NULL, NULL);
        break;
    default:
        oncRPCSvcErrNoProc(req);
    }
}
```

## SEE ALSO

[oncRPCSvcErrDecode(3)](oncRPCSvcErrDecode.3.md),
[oncRPCSvcErrNoProg(3)](oncRPCSvcErrNoProg.3.md),
[oncRPCServerRegister(3)](oncRPCServerRegister.3.md)
