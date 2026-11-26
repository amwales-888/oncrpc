# oncRPCServerSetTls(3)

## NAME

oncRPCServerSetTls - offer or require TLS on a server

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCServerSetTls(oncRPCServer *srv, oncRPCTlsConfig *cfg, int required);
```

## DESCRIPTION

`oncRPCServerSetTls()` makes the server offer TLS 1.3 (RFC 9289,
RPC-with-TLS) on its TCP connections. A client that asks for it, with the
AUTH_TLS probe, gets the answer `STARTTLS` and the connection is upgraded;
everything after that is encrypted. Clients that do not ask continue in the
clear.

- `cfg` is a server configuration from
  [oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md) with a certificate
  and key. It also says whether clients must present certificates
  (mutual TLS; see [oncRPCTlsConfigSetVerify(3)](oncRPCTlsConfigSetVerify.3.md)).
  The server keeps its own reference.
- With `required` non-zero, requests that did not arrive over TLS - on TCP
  connections that were not upgraded, and over UDP - are refused with
  AUTH_ERROR and reason `ONCRPC_AUTH_TOOWEAK`. Local sockets are not
  affected.

Dispatch routines can tell whether a request came over TLS with
[oncRPCSvcTls(3)](oncRPCSvcTls.3.md), and who the client is with
[oncRPCSvcTlsPeer(3)](oncRPCSvcTlsPeer.3.md).

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if an argument is `NULL`, the server is
closing, or `cfg` is a client configuration or has no certificate.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

/* cert_pem, key_pem: the server's certificate and key, read at start-up. */
int require_tls(oncRPCServer *server, const char *cert_pem, const char *key_pem)
{
    oncRPCTlsConfig *tls;
    int ok;

    if (oncRPCTlsConfigCreate(oncRPCServerAllocator(server), ONCRPC_TLS_SERVER,
                              &tls) != ONCRPC_OK)
        return -1;
    ok = oncRPCTlsConfigSetCertificate(tls, cert_pem, key_pem) == ONCRPC_OK &&
         oncRPCServerSetTls(server, tls, 1) == ONCRPC_OK;
    oncRPCTlsConfigFree(tls);   /* the server keeps its own reference */
    if (!ok)
        fprintf(stderr, "cannot set up TLS\n");
    return ok ? 0 : -1;
}
```

## SEE ALSO

[oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md),
[oncRPCClientSetTls(3)](oncRPCClientSetTls.3.md),
[oncRPCSvcTls(3)](oncRPCSvcTls.3.md),
[oncRPCSvcTlsPeer(3)](oncRPCSvcTlsPeer.3.md)
