# oncRPCSvcVerf(3)

## NAME

oncRPCSvcVerf - verifier a request arrived with

## SYNOPSIS

```c
#include <oncrpc.h>

const oncRPCAuthData *oncRPCSvcVerf(const oncRPCSvcReq *req);
```

## DESCRIPTION

`oncRPCSvcVerf()` returns the verifier of `req` exactly as it arrived.
A verifier proves the credential: for instance a signature over the call
header made with a key only the genuine caller has. AUTH_NONE and AUTH_SYS
calls carry an empty AUTH_NONE verifier.

An authenticator checks it against the header bytes it receives (see
[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md)).

## RETURN VALUE

A pointer valid until the request is finished. Never `NULL`.

## EXAMPLE

Check a verifier holding a 4-byte checksum of the header:

```c
#include <string.h>
#include <oncrpc.h>

#define CHECKSUM_FLAVOR 390001u

uint32_t checksum(const uint8_t *data, size_t len);   /* the agreed function */

oncRPCAuthStat check_verifier(oncRPCSvcReq *req, const uint8_t *header, size_t len,
                              void *ctx)
{
    const oncRPCAuthData *verf = oncRPCSvcVerf(req);
    uint32_t sum = checksum(header, len);
    uint8_t want[4];
    (void)ctx;

    want[0] = (uint8_t)(sum >> 24);
    want[1] = (uint8_t)(sum >> 16);
    want[2] = (uint8_t)(sum >> 8);
    want[3] = (uint8_t)sum;
    if (verf->flavor != CHECKSUM_FLAVOR || verf->len != 4 || memcmp(verf->body, want, 4) != 0)
        return ONCRPC_AUTH_BADVERF;
    return ONCRPC_AUTH_OK;
}
```

## SEE ALSO

[oncRPCSvcCred(3)](oncRPCSvcCred.3.md),
[oncRPCSvcSetReplyVerf(3)](oncRPCSvcSetReplyVerf.3.md),
[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md)
