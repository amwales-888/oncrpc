# oncRPCSvcTls(3)

## NAME

oncRPCSvcTls - whether a request arrived over TLS

## SYNOPSIS

```c
#include <oncrpc.h>

int oncRPCSvcTls(const oncRPCSvcReq *req);
```

## DESCRIPTION

`oncRPCSvcTls()` tells whether `req` arrived on a connection protected by
TLS (see [oncRPCServerSetTls(3)](oncRPCServerSetTls.3.md)). A server that
offers TLS without requiring it can use this to allow some procedures in
the clear and keep others for protected connections.

## RETURN VALUE

Non-zero if the request came over TLS, 0 otherwise.

## EXAMPLE

```c
#include <oncrpc.h>

#define SET_PASSWORD 4u

void dispatch(oncRPCSvcReq *req)
{
    if (req->proc == SET_PASSWORD && !oncRPCSvcTls(req)) {
        oncRPCSvcErrAuth(req, ONCRPC_AUTH_TOOWEAK);   /* never in the clear */
        return;
    }
    oncRPCSvcErrNoProc(req);   /* real procedures omitted */
}
```

## SEE ALSO

[oncRPCSvcTlsPeer(3)](oncRPCSvcTlsPeer.3.md),
[oncRPCServerSetTls(3)](oncRPCServerSetTls.3.md)
