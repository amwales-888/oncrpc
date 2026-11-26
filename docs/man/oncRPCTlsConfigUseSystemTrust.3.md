# oncRPCTlsConfigUseSystemTrust(3)

## NAME

oncRPCTlsConfigUseSystemTrust - trust the system's certificate store

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCTlsConfigUseSystemTrust(oncRPCTlsConfig *cfg);
```

## DESCRIPTION

`oncRPCTlsConfigUseSystemTrust()` makes `cfg` also trust OpenSSL's default
certificate store: the public certificate authorities installed on the
system. Use it for servers whose certificates come from a public
authority.

OpenSSL may read certificate files from the store while it checks a
certificate, which blocks the event loop briefly. For a private service,
trusting its own authority with
[oncRPCTlsConfigAddTrusted(3)](oncRPCTlsConfigAddTrusted.3.md) avoids
that, and is also stricter.

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if `cfg` is `NULL`; `ONCRPC_ERR_TLS` if
OpenSSL could not set up its default store.

## EXAMPLE

```c
#include <oncrpc.h>

oncRPCStat public_client_tls(const oncRPCAllocator *a, oncRPCTlsConfig **out)
{
    oncRPCStat st = oncRPCTlsConfigCreate(a, ONCRPC_TLS_CLIENT, out);

    if (st == ONCRPC_OK && (st = oncRPCTlsConfigUseSystemTrust(*out)) != ONCRPC_OK) {
        oncRPCTlsConfigFree(*out);
        *out = NULL;
    }
    return st;
}
```

## SEE ALSO

[oncRPCTlsConfigAddTrusted(3)](oncRPCTlsConfigAddTrusted.3.md),
[oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md)
