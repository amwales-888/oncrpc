# oncRPCTlsConfigCreate(3)

## NAME

oncRPCTlsConfigCreate - create a TLS configuration

## SYNOPSIS

```c
#include <oncrpc.h>

typedef enum oncRPCTlsRole {
    ONCRPC_TLS_CLIENT,
    ONCRPC_TLS_SERVER
} oncRPCTlsRole;

oncRPCStat oncRPCTlsConfigCreate(const oncRPCAllocator *alloc, oncRPCTlsRole role,
                                 oncRPCTlsConfig **out);
```

## DESCRIPTION

`oncRPCTlsConfigCreate()` creates a TLS configuration for one side,
client or server, and stores it in `*out`. A configuration holds this
side's certificate and key, the certificates it trusts, and how strictly it
checks the other side. Any number of clients or servers can share one.

liboncrpc follows RFC 9289 (RPC-with-TLS): a TCP connection starts in the
clear and is upgraded to TLS 1.3 when the client asks. TLS is provided by
OpenSSL 3; older TLS versions are not offered.

The defaults are:

- **client:** the server's certificate must chain to a trusted
  certificate and carry the server's name. No certificate is trusted yet:
  add some with [oncRPCTlsConfigAddTrusted(3)](oncRPCTlsConfigAddTrusted.3.md)
  or [oncRPCTlsConfigUseSystemTrust(3)](oncRPCTlsConfigUseSystemTrust.3.md).
- **server:** clients are not asked for certificates. A certificate and key
  must be set with
  [oncRPCTlsConfigSetCertificate(3)](oncRPCTlsConfigSetCertificate.3.md)
  before the configuration can be used.

Certificates and keys are given as PEM text in memory, so the library
never reads files; read them however suits the application, typically
once at start-up.

Hand the configuration to [oncRPCClientSetTls(3)](oncRPCClientSetTls.3.md)
or [oncRPCServerSetTls(3)](oncRPCServerSetTls.3.md), then release the
application's hold with [oncRPCTlsConfigFree(3)](oncRPCTlsConfigFree.3.md).

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if `alloc` or `out` is `NULL` or `role`
is unknown; `ONCRPC_ERR_NOMEM` if the allocator failed; `ONCRPC_ERR_TLS`
if OpenSSL could not set up a TLS 1.3 context.

## EXAMPLE

A server configuration with mutual TLS: clients need a certificate signed
by `clients_ca_pem`:

```c
#include <oncrpc.h>

oncRPCTlsConfig *server_tls(const oncRPCAllocator *a, const char *cert_pem,
                            const char *key_pem, const char *clients_ca_pem)
{
    oncRPCTlsConfig *tls;

    if (oncRPCTlsConfigCreate(a, ONCRPC_TLS_SERVER, &tls) != ONCRPC_OK)
        return NULL;
    if (oncRPCTlsConfigSetCertificate(tls, cert_pem, key_pem) != ONCRPC_OK ||
        oncRPCTlsConfigAddTrusted(tls, clients_ca_pem) != ONCRPC_OK ||
        oncRPCTlsConfigSetVerify(tls, ONCRPC_TLS_VERIFY_REQUIRE) != ONCRPC_OK) {
        oncRPCTlsConfigFree(tls);
        return NULL;
    }
    return tls;
}
```

## SEE ALSO

[oncRPCTlsConfigFree(3)](oncRPCTlsConfigFree.3.md),
[oncRPCTlsConfigSetCertificate(3)](oncRPCTlsConfigSetCertificate.3.md),
[oncRPCTlsConfigAddTrusted(3)](oncRPCTlsConfigAddTrusted.3.md),
[oncRPCTlsConfigSetVerify(3)](oncRPCTlsConfigSetVerify.3.md),
[oncRPCClientSetTls(3)](oncRPCClientSetTls.3.md),
[oncRPCServerSetTls(3)](oncRPCServerSetTls.3.md)
