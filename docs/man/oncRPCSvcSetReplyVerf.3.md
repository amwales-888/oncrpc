# oncRPCSvcSetReplyVerf(3)

## NAME

oncRPCSvcSetReplyVerf - verifier to send with a reply

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCSvcSetReplyVerf(oncRPCSvcReq *req, uint32_t flavor,
                                 const uint8_t *body, uint32_t len);
```

## DESCRIPTION

`oncRPCSvcSetReplyVerf()` sets the verifier that goes with the reply to
`req`: flavor `flavor` and the `len` bytes at `body` (at most
`ONCRPC_MAX_AUTH_BYTES`), copied. Without it, replies carry an empty
AUTH_NONE verifier.

A reply verifier lets the client check that the answer came from the
genuine server, for instance a value derived from the request's
transaction id with a shared key. The client checks it with the `validate`
function of its authentication (see
[oncRPCClientSetAuth(3)](oncRPCClientSetAuth.3.md)).

The verifier is sent with accepted replies, successful or not. Rejected
replies (authentication errors, RPC version mismatch) carry none.

It is usually called by an authenticator, but a dispatch routine may call
it too, as long as the request is not finished.

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if `req` is `NULL` or finished, `len`
is too large, or `body` is `NULL` while `len` is not 0;
`ONCRPC_ERR_NOMEM` if the allocator failed.

## EXAMPLE

Answer with the transaction id plus one, as proof the server saw the call:

```c
#include <oncrpc.h>

#define XID_FLAVOR 390002u

oncRPCAuthStat sign_reply(oncRPCSvcReq *req, const uint8_t *header, size_t len, void *ctx)
{
    uint32_t v = req->xid + 1;
    uint8_t body[4];
    (void)header;
    (void)len;
    (void)ctx;

    body[0] = (uint8_t)(v >> 24);
    body[1] = (uint8_t)(v >> 16);
    body[2] = (uint8_t)(v >> 8);
    body[3] = (uint8_t)v;
    if (oncRPCSvcSetReplyVerf(req, XID_FLAVOR, body, sizeof(body)) != ONCRPC_OK)
        return ONCRPC_AUTH_FAILED;
    return ONCRPC_AUTH_OK;
}
```

## SEE ALSO

[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md),
[oncRPCSvcVerf(3)](oncRPCSvcVerf.3.md),
[oncRPCClientSetAuth(3)](oncRPCClientSetAuth.3.md)
