# oncRPCSvcCred(3)

## NAME

oncRPCSvcCred - credential a request arrived with

## SYNOPSIS

```c
#include <oncrpc.h>

const oncRPCAuthData *oncRPCSvcCred(const oncRPCSvcReq *req);
```

## DESCRIPTION

`oncRPCSvcCred()` returns the credential of `req` exactly as it arrived:
its flavor, length and up to `ONCRPC_MAX_AUTH_BYTES` bytes of body (see
`oncRPCAuthData` in [oncRPCClientSetAuth(3)](oncRPCClientSetAuth.3.md)). A
request without credentials gives flavor `ONCRPC_AUTH_NONE` with an empty
body.

For AUTH_SYS the decoded form is also in `req->authSys`, and is easier to
use. The raw body is for authenticators of other flavors (see
[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md)) and for logging.

## RETURN VALUE

A pointer valid until the request is finished. Never `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void log_flavor(const oncRPCSvcReq *req)
{
    const oncRPCAuthData *cred = oncRPCSvcCred(req);

    switch (cred->flavor) {
    case ONCRPC_AUTH_NONE:
        printf("anonymous caller\n");
        break;
    case ONCRPC_AUTH_SYS:
        printf("uid %u on %s\n", (unsigned)req->authSys->uid, req->authSys->machinename);
        break;
    default:
        printf("flavor %u, %u bytes\n", (unsigned)cred->flavor, (unsigned)cred->len);
    }
}
```

## SEE ALSO

[oncRPCSvcVerf(3)](oncRPCSvcVerf.3.md),
[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md),
[oncRPCXdrAuthSys(3)](oncRPCXdrAuthSys.3.md)
