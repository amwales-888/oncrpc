# oncRPCTlsConfigSetCertificate(3)

## NAME

oncRPCTlsConfigSetCertificate - this side's certificate and key

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCTlsConfigSetCertificate(oncRPCTlsConfig *cfg, const char *certPem,
                                         const char *keyPem);
```

## DESCRIPTION

`oncRPCTlsConfigSetCertificate()` sets the certificate this side presents
and its private key, both as PEM text. `certPem` holds the certificate
followed by any intermediate certificates needed to reach a trusted
authority. `keyPem` holds the unencrypted private key, which must match the
certificate. Both are copied.

A server needs one. A client needs one only for servers that ask for
client certificates (mutual TLS); it is sent when asked.

Setting a certificate again replaces the previous one, for connections
made afterwards.

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if an argument is `NULL`;
`ONCRPC_ERR_NOMEM` if OpenSSL ran out of memory; `ONCRPC_ERR_TLS` if the
text holds no certificate or key, or they do not match.

## EXAMPLE

Read the PEM files with standard C, then set them:

```c
#include <stdio.h>
#include <stdlib.h>
#include <oncrpc.h>

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text = NULL;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (text = malloc((size_t)n + 1)) != NULL) {
        if (fread(text, 1, (size_t)n, f) == (size_t)n) {
            text[n] = '\0';
        } else {
            free(text);
            text = NULL;
        }
    }
    fclose(f);
    return text;
}

oncRPCStat load_identity(oncRPCTlsConfig *tls, const char *cert_path, const char *key_path)
{
    char *cert = read_file(cert_path), *key = read_file(key_path);
    oncRPCStat st = ONCRPC_ERR_FAILED;

    if (cert != NULL && key != NULL)
        st = oncRPCTlsConfigSetCertificate(tls, cert, key);
    free(cert);
    free(key);   /* the configuration has its own copy */
    return st;
}
```

## SEE ALSO

[oncRPCTlsConfigAddTrusted(3)](oncRPCTlsConfigAddTrusted.3.md),
[oncRPCTlsConfigCreate(3)](oncRPCTlsConfigCreate.3.md),
[oncRPCServerSetTls(3)](oncRPCServerSetTls.3.md)
