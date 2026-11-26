# oncRPCSvcErrProgVers(3)

## NAME

oncRPCSvcErrProgVers - reply that the program version is not served

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCSvcErrProgVers(oncRPCSvcReq *req, uint32_t low, uint32_t high);
```

## DESCRIPTION

`oncRPCSvcErrProgVers()` answers `req` with PROG_MISMATCH, meaning the
program is served but not in the version asked for, together with the
lowest (`low`) and highest (`high`) versions that are. It finishes the
request. The client's call fails with `ONCRPC_ERR_PROGVERSMISMATCH`, and
the range is in [oncRPCClientLastError(3)](oncRPCClientLastError.3.md).

The library already sends this reply, with the right range, for versions
the server has not registered. A dispatch routine needs it only when it
handles several versions itself and wants to refuse one.

Once finished, a request may be freed at once: do not use `req` again. A
`NULL` or already finished request is ignored.

## EXAMPLE

One dispatch routine registered for versions 1 to 3, where version 1 has
been withdrawn:

```c
#include <oncrpc.h>

void dispatch(oncRPCSvcReq *req)
{
    if (req->vers < 2) {
        oncRPCSvcErrProgVers(req, 2, 3);
        return;
    }
    oncRPCSvcErrNoProc(req);   /* real procedures omitted */
}
```

## SEE ALSO

[oncRPCSvcErrNoProg(3)](oncRPCSvcErrNoProg.3.md),
[oncRPCClientLastError(3)](oncRPCClientLastError.3.md)
