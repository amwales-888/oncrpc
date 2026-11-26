# oncRPCTlsConfigAddTrusted(3)

## NAME

oncRPCTlsConfigAddTrusted - trust certificates as issuers

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCTlsConfigAddTrusted(oncRPCTlsConfig *cfg, const char *pem);
```

## DESCRIPTION

`oncRPCTlsConfigAddTrusted()` adds every certificate in the PEM text
`pem` to those `cfg` trusts as issuers of the other side's certificate.
It may be called several times.

A client uses it to trust the authority that signed its servers'
certificates, which is the usual arrangement for a private service. A
server uses it, with mutual TLS, to trust the authority that signed its
clients' certificates.

To trust the certificates of the operating system instead, or as well,
use [oncRPCTlsConfigUseSystemTrust(3)](oncRPCTlsConfigUseSystemTrust.3.md).

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if an argument is `NULL`;
`ONCRPC_ERR_NOMEM` if OpenSSL ran out of memory; `ONCRPC_ERR_TLS` if the
text holds no certificate, or one could not be added.

## EXAMPLE

```c
#include <oncrpc.h>

/* A private authority's certificate, compiled into the program. */
extern const char company_ca_pem[];

oncRPCStat trust_company(oncRPCTlsConfig *tls)
{
    return oncRPCTlsConfigAddTrusted(tls, company_ca_pem);
}
```

## SEE ALSO

[oncRPCTlsConfigUseSystemTrust(3)](oncRPCTlsConfigUseSystemTrust.3.md),
[oncRPCTlsConfigSetVerify(3)](oncRPCTlsConfigSetVerify.3.md),
[oncRPCTlsConfigSetCertificate(3)](oncRPCTlsConfigSetCertificate.3.md)
