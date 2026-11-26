# oncRPCSvcErrNoProg(3)

## NAME

oncRPCSvcErrNoProg - reply that the program is not served

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCSvcErrNoProg(oncRPCSvcReq *req);
```

## DESCRIPTION

`oncRPCSvcErrNoProg()` answers `req` with PROG_UNAVAIL, meaning the
program is not available here, and finishes the request. The client's call
fails with `ONCRPC_ERR_PROGUNAVAIL`.

The library already sends this reply for programs the server has not
registered, so a dispatch routine needs it only to turn callers away on
its own terms, for instance while a service is in maintenance.

Once finished, a request may be freed at once: do not use `req` again. A
`NULL` or already finished request is ignored.

## EXAMPLE

```c
#include <oncrpc.h>

int in_maintenance;   /* set by an administrative command */

void dispatch(oncRPCSvcReq *req)
{
    if (in_maintenance && req->proc != 0) {
        oncRPCSvcErrNoProg(req);
        return;
    }
    oncRPCSvcErrNoProc(req);   /* real procedures omitted */
}
```

## SEE ALSO

[oncRPCSvcErrProgVers(3)](oncRPCSvcErrProgVers.3.md),
[oncRPCServerUnregister(3)](oncRPCServerUnregister.3.md)
