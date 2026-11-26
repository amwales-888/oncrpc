# oncRPCClientSetTls(3)

## NAME

oncRPCClientSetTls - make a client use TLS

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCClientSetTls(oncRPCClient *clnt, oncRPCTlsConfig *cfg,
                              const char *serverName);
```

## DESCRIPTION

`oncRPCClientSetTls()` makes the TCP client `clnt` protect its connections
with TLS 1.3, following RFC 9289 (RPC-with-TLS). As soon as a connection
is made, and whenever it is remade, the client asks the server to start
TLS, checks the server's certificate according to `cfg`, and only then
sends its calls. Everything after that, including the credentials, is
encrypted.

- `cfg` is a client configuration from
  [oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md) with trusted
  certificates, and a certificate of its own if the server asks for one.
  The client keeps its own reference, so the application may free its
  hold with [oncRPCTlsConfigFree(3)](oncRPCTlsConfigFree.3.md) afterwards.
- `serverName` is the name the server's certificate must carry: a DNS
  name or an IP address. A DNS name is also sent to the server (SNI).
  `NULL` means the `host` given to
  [oncRPCClientCreate(3)](oncRPCClientCreate.3.md).

Call it straight after `oncRPCClientCreate()`, before the loop runs again.
Calls started meanwhile wait until TLS is up. If TLS cannot be set up -
the server does not offer it, or the certificate does not check out - the
ready callback and the calls fail with `ONCRPC_ERR_TLS`, and
[oncRPCClientLastError(3)](oncRPCClientLastError.3.md)'s `detail` says
why.

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if `clnt` or `cfg` is `NULL`, `cfg` is a
server configuration, the client does not use TCP, or its connection is
already up; `ONCRPC_ERR_NOMEM` if the allocator failed.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

/* ca_pem: the certificate of the authority that signed the server's. */
oncRPCClient *secure_client(uv_loop_t *loop, const oncRPCAllocator *a, const char *ca_pem)
{
    oncRPCTlsConfig *tls;
    oncRPCClient *client;

    if (oncRPCTlsConfigCreate(a, ONCRPC_TLS_CLIENT, &tls) != ONCRPC_OK)
        return NULL;
    if (oncRPCTlsConfigAddTrusted(tls, ca_pem) != ONCRPC_OK ||
        oncRPCClientCreate(loop, a, "rpc.example.org", 5010, 0x20000110, 1,
                           ONCRPC_PROTO_TCP, NULL, NULL, &client) != ONCRPC_OK) {
        oncRPCTlsConfigFree(tls);
        return NULL;
    }
    if (oncRPCClientSetTls(client, tls, NULL) != ONCRPC_OK) {   /* name: rpc.example.org */
        oncRPCClientClose(client, NULL, NULL);
        client = NULL;
    }
    oncRPCTlsConfigFree(tls);   /* the client keeps its own reference */
    return client;
}
```

## SEE ALSO

[oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md),
[oncRPCServerSetTls(3)](oncRPCServerSetTls.3.md),
[oncRPCClientLastError(3)](oncRPCClientLastError.3.md)
