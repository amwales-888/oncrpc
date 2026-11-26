# oncRPCTlsConfigSetVerify(3)

## NAME

oncRPCTlsConfigSetVerify - how strictly to check the other side

## SYNOPSIS

```c
#include <oncrpc.h>

typedef enum oncRPCTlsVerify {
    ONCRPC_TLS_VERIFY_NONE,
    ONCRPC_TLS_VERIFY_PEER,
    ONCRPC_TLS_VERIFY_REQUIRE
} oncRPCTlsVerify;

oncRPCStat oncRPCTlsConfigSetVerify(oncRPCTlsConfig *cfg, oncRPCTlsVerify mode);
```

## DESCRIPTION

`oncRPCTlsConfigSetVerify()` sets how `cfg` checks the other side's
certificate:

| `mode` | Client | Server |
|---|---|---|
| `ONCRPC_TLS_VERIFY_NONE` | do not check the server's certificate: for testing only, since anyone could pose as the server | do not ask clients for certificates (the server default) |
| `ONCRPC_TLS_VERIFY_PEER` | the server's certificate must chain to a trusted certificate and carry the server's name (the client default) | ask clients for a certificate, and check any that is given |
| `ONCRPC_TLS_VERIFY_REQUIRE` | same as `VERIFY_PEER` | clients must present a certificate that checks out (mutual TLS) |

A server that verifies clients needs trusted certificates (see
[oncRPCTlsConfigAddTrusted(3)](oncRPCTlsConfigAddTrusted.3.md)), and can
read who the client is with [oncRPCSvcTlsPeer(3)](oncRPCSvcTlsPeer.3.md).

## RETURN VALUE

`ONCRPC_OK`, or `ONCRPC_ERR_INVALID` if `cfg` is `NULL` or `mode` is
unknown.

## EXAMPLE

```c
#include <oncrpc.h>

oncRPCStat require_client_certificates(oncRPCTlsConfig *server_tls)
{
    return oncRPCTlsConfigSetVerify(server_tls, ONCRPC_TLS_VERIFY_REQUIRE);
}
```

## SEE ALSO

[oncRPCTlsConfigAddTrusted(3)](oncRPCTlsConfigAddTrusted.3.md),
[oncRPCSvcTlsPeer(3)](oncRPCSvcTlsPeer.3.md),
[oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md)
