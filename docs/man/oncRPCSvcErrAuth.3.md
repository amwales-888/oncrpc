# oncRPCSvcErrAuth(3)

## NAME

oncRPCSvcErrAuth - reject a request's credentials

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCSvcErrAuth(oncRPCSvcReq *req, oncRPCAuthStat why);
```

## DESCRIPTION

`oncRPCSvcErrAuth()` rejects `req` with AUTH_ERROR and the reason `why`,
and finishes the request. The client's call fails with
`ONCRPC_ERR_AUTHERROR`, and `why` is in
[oncRPCClientLastError(3)](oncRPCClientLastError.3.md).

| `why` | Meaning |
|---|---|
| `ONCRPC_AUTH_BADCRED` | the credential is malformed |
| `ONCRPC_AUTH_REJECTEDCRED` | the credential is refused; the client may get a new one and retry |
| `ONCRPC_AUTH_BADVERF` | the verifier is malformed or wrong |
| `ONCRPC_AUTH_REJECTEDVERF` | the verifier has expired or was replayed |
| `ONCRPC_AUTH_TOOWEAK` | the caller is not allowed this, with these credentials |
| `ONCRPC_AUTH_FAILED` | the check could not be made |

Checks that apply to every procedure belong in an authenticator (see
[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md)). This function is for
checks that depend on the procedure or its arguments.

Once finished, a request may be freed at once: do not use `req` again. A
`NULL` or already finished request is ignored.

## EXAMPLE

Only root may call the SHUTDOWN procedure:

```c
#include <oncrpc.h>

#define SHUTDOWN 9u

void dispatch(oncRPCSvcReq *req)
{
    if (req->proc == SHUTDOWN && (req->authSys == NULL || req->authSys->uid != 0)) {
        oncRPCSvcErrAuth(req, ONCRPC_AUTH_TOOWEAK);
        return;
    }
    oncRPCSvcErrNoProc(req);   /* real procedures omitted */
}
```

## SEE ALSO

[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md),
[oncRPCClientLastError(3)](oncRPCClientLastError.3.md)
