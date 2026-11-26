# oncRPCTlsConfigFree(3)

## NAME

oncRPCTlsConfigFree - release the application's hold on a TLS configuration

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCTlsConfigFree(oncRPCTlsConfig *cfg);
```

## DESCRIPTION

`oncRPCTlsConfigFree()` gives up the application's reference to `cfg`.
Clients and servers using the configuration hold references of their own,
so it stays alive until the last of them closes; then it is released.
Freeing it straight after handing it to them is therefore normal. A `NULL`
configuration is ignored.

Do not use `cfg` after this call.

## EXAMPLE

```c
#include <oncrpc.h>

oncRPCStat use_tls(oncRPCClient *a, oncRPCClient *b, const oncRPCAllocator *alloc,
                   const char *ca_pem)
{
    oncRPCTlsConfig *tls;
    oncRPCStat st = oncRPCTlsConfigCreate(alloc, ONCRPC_TLS_CLIENT, &tls);

    if (st != ONCRPC_OK)
        return st;
    st = oncRPCTlsConfigAddTrusted(tls, ca_pem);
    if (st == ONCRPC_OK)
        st = oncRPCClientSetTls(a, tls, NULL);
    if (st == ONCRPC_OK)
        st = oncRPCClientSetTls(b, tls, NULL);
    oncRPCTlsConfigFree(tls);   /* a and b keep it alive */
    return st;
}
```

## SEE ALSO

[oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md)
