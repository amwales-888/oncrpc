# oncRPCSvcErrSystem(3)

## NAME

oncRPCSvcErrSystem - reply that the server failed

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCSvcErrSystem(oncRPCSvcReq *req);
```

## DESCRIPTION

`oncRPCSvcErrSystem()` answers `req` with SYSTEM_ERR, meaning the server
could not carry out the procedure for a reason of its own (out of memory,
a failed disk, a back end that is down), and finishes the request. The
client's call fails with `ONCRPC_ERR_SYSTEMERROR`.

Errors that are part of the procedure's meaning (file not found,
permission denied) are better returned as results, which the client can
act on; SYSTEM_ERR carries no detail.

Once finished, a request may be freed at once: do not use `req` again. A
`NULL` or already finished request is ignored.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

static int xdr_line(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrString(xdrs, (char **)obj, 255);
}

/* Return the first line of the configuration file. */
void read_config_proc(oncRPCSvcReq *req)
{
    FILE *f = fopen("/etc/myservice.conf", "r");
    char line[256];
    char *p = line;

    if (f == NULL) {
        oncRPCSvcErrSystem(req);   /* the client cannot do anything about this */
        return;
    }
    if (fgets(line, sizeof(line), f) == NULL)
        line[0] = '\0';
    fclose(f);
    oncRPCSvcReply(req, xdr_line, &p);
}
```

## SEE ALSO

[oncRPCSvcErrDecode(3)](oncRPCSvcErrDecode.3.md),
[oncRPCSvcReply(3)](oncRPCSvcReply.3.md)
