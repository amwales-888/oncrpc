# oncRPCServerSetAuth(3)

## NAME

oncRPCServerSetAuth - check callers of an authentication flavor

## SYNOPSIS

```c
#include <oncrpc.h>

typedef oncRPCAuthStat (*oncRPCSvcAuthFn)(oncRPCSvcReq *req, const uint8_t *header,
                                          size_t len, void *ctx);

oncRPCStat oncRPCServerSetAuth(oncRPCServer *srv, uint32_t flavor,
                               oncRPCSvcAuthFn fn, void *ctx);
```

## DESCRIPTION

`oncRPCServerSetAuth()` installs `fn` as the authenticator for
authentication flavor `flavor`. It runs for every request that carries a
credential of that flavor, before the request is dispatched, and decides
whether it goes through. `fn` `NULL` removes the authenticator.

The authenticator receives the request (its `ctx` is not set yet), and in
`header`/`len` the call as it arrived from the transaction id to the end of
the credential, for verifiers that sign it. It reads the credential and
verifier with [oncRPCSvcCred(3)](oncRPCSvcCred.3.md) and
[oncRPCSvcVerf(3)](oncRPCSvcVerf.3.md), and may:

- set the reply's verifier with
  [oncRPCSvcSetReplyVerf(3)](oncRPCSvcSetReplyVerf.3.md);
- leave something for the dispatch routine, such as the identity it
  established, in `req->authCtx`.

It returns `ONCRPC_AUTH_OK` to let the request through, or a reason such as
`ONCRPC_AUTH_REJECTEDCRED`, `ONCRPC_AUTH_BADVERF` or `ONCRPC_AUTH_TOOWEAK`,
which the library sends back as an AUTH_ERROR rejection. It must not
finish the request.

Without an authenticator, AUTH_NONE and AUTH_SYS requests are let through
and every other flavor is refused with `ONCRPC_AUTH_REJECTEDCRED`. With
one, the flavor is accepted whenever the authenticator says so. That
gives three common uses:

- accept a flavor of your own (see
  [oncRPCClientSetAuth(3)](oncRPCClientSetAuth.3.md) for the client side);
- refuse unauthenticated callers, with an authenticator for AUTH_NONE that
  returns `ONCRPC_AUTH_TOOWEAK`;
- decide which AUTH_SYS callers may call, with an authenticator for
  AUTH_SYS (the credential is already decoded in `req->authSys`).

AUTH_TLS cannot have an authenticator: it is the request to start TLS.

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if `srv` is `NULL` or closing, or
`flavor` is `ONCRPC_AUTH_TLS`; `ONCRPC_ERR_NOMEM` if the allocator failed.

## EXAMPLE

Accept AUTH_SYS callers in group 100 only, and refuse anonymous ones:

```c
#include <oncrpc.h>

static oncRPCAuthStat staff_only(oncRPCSvcReq *req, const uint8_t *header, size_t len,
                                 void *ctx)
{
    uint32_t i, group = *(const uint32_t *)ctx;
    const oncRPCAuthSysParams *p = req->authSys;
    (void)header;
    (void)len;

    if (p->gid == group)
        return ONCRPC_AUTH_OK;
    for (i = 0; i < p->ngids; i++)
        if (p->gids[i] == group)
            return ONCRPC_AUTH_OK;
    return ONCRPC_AUTH_TOOWEAK;
}

static oncRPCAuthStat nobody(oncRPCSvcReq *req, const uint8_t *header, size_t len, void *ctx)
{
    (void)req;
    (void)header;
    (void)len;
    (void)ctx;
    return ONCRPC_AUTH_TOOWEAK;
}

oncRPCStat restrict_to_staff(oncRPCServer *server)
{
    static const uint32_t staff = 100;
    oncRPCStat st = oncRPCServerSetAuth(server, ONCRPC_AUTH_SYS, staff_only, (void *)&staff);
    if (st == ONCRPC_OK)
        st = oncRPCServerSetAuth(server, ONCRPC_AUTH_NONE, nobody, NULL);
    return st;
}
```

`examples/11_auth` in the source tree has a custom flavor with signed
verifiers on both sides.

## SEE ALSO

[oncRPCSvcCred(3)](oncRPCSvcCred.3.md),
[oncRPCSvcSetReplyVerf(3)](oncRPCSvcSetReplyVerf.3.md),
[oncRPCClientSetAuth(3)](oncRPCClientSetAuth.3.md),
[oncRPCSvcErrAuth(3)](oncRPCSvcErrAuth.3.md)
