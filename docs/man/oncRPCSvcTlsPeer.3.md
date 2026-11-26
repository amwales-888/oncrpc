# oncRPCSvcTlsPeer(3)

## NAME

oncRPCSvcTlsPeer - certificate subject of a TLS client

## SYNOPSIS

```c
#include <oncrpc.h>

int oncRPCSvcTlsPeer(const oncRPCSvcReq *req, char *buf, size_t len);
```

## DESCRIPTION

With mutual TLS, a client presents a certificate of its own, which the
server checks against its trusted certificates (see
[oncRPCTlsConfigSetVerify(3)](oncRPCTlsConfigSetVerify.3.md)).
`oncRPCSvcTlsPeer()` writes the subject of that certificate into `buf`, as
an RFC 2253 string such as `"CN=alice,O=Example"`, cut to fit in `len`
bytes with the terminating NUL.

The certificate is proof of identity, so the subject can be used to decide
what the caller may do.

## RETURN VALUE

0 on success; -1 if the request did not come over TLS, the client
presented no certificate that checked out, or an argument is `NULL`.

## EXAMPLE

```c
#include <string.h>
#include <oncrpc.h>

int is_admin(const oncRPCSvcReq *req)
{
    char subject[256];

    if (oncRPCSvcTlsPeer(req, subject, sizeof(subject)) != 0)
        return 0;
    return strcmp(subject, "CN=admin,O=Example") == 0;
}
```

## SEE ALSO

[oncRPCSvcTls(3)](oncRPCSvcTls.3.md),
[oncRPCTlsConfigSetVerify(3)](oncRPCTlsConfigSetVerify.3.md),
[oncRPCServerSetTls(3)](oncRPCServerSetTls.3.md)
