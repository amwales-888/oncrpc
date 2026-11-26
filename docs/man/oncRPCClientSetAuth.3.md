# oncRPCClientSetAuth(3)

## NAME

oncRPCClientSetAuth - use an application-defined authentication flavor

## SYNOPSIS

```c
#include <oncrpc.h>

typedef struct oncRPCClientAuth {
    int  (*credential)(void *data, uint32_t xid, uint32_t proc, oncRPCAuthData *cred);
    int  (*verifier)(void *data, const uint8_t *header, size_t len, oncRPCAuthData *verf);
    int  (*validate)(void *data, uint32_t xid, const oncRPCAuthData *verf);
    int  (*refresh)(void *data, oncRPCAuthStat why);
    void (*release)(void *data);
} oncRPCClientAuth;

oncRPCStat oncRPCClientSetAuth(oncRPCClient *clnt, const oncRPCClientAuth *auth, void *data);
```

## DESCRIPTION

Every call carries a credential and a verifier: a flavor number and up to
`ONCRPC_MAX_AUTH_BYTES` (400) bytes each, held in an `oncRPCAuthData`:

```c
typedef struct oncRPCAuthData {
    uint32_t flavor;
    uint32_t len;
    uint8_t  body[ONCRPC_MAX_AUTH_BYTES];
} oncRPCAuthData;
```

The library makes AUTH_NONE and AUTH_SYS itself.
`oncRPCClientSetAuth()` plugs in any other flavor: the functions in
`auth` produce and check the authentication data, and each receives
`data`.

| Function | Called | Must |
|---|---|---|
| `credential` | for each call (required) | fill in `cred` for the call with this `xid` and `proc`; return 0, or -1 to refuse the call |
| `verifier` | for each call; may be `NULL` (empty AUTH_NONE verifier) | fill in `verf`; `header`/`len` is the call as encoded so far, from the xid to the end of the credential, for verifiers that sign it; return 0 or -1 |
| `validate` | for each accepted reply; may be `NULL` (any verifier accepted) | return 0 if the reply's verifier is good; -1 fails the call |
| `refresh` | when the server refuses the credential; may be `NULL` (never retry) | return 1 to send the call again with a new xid and fresh credential and verifier (at most twice per call), 0 to let it fail |
| `release` | when another authentication is set or the client closes; may be `NULL` | free whatever `data` holds |

The functions run inside `oncRPCClientCall()` and while replies are being
handled, so they must not call functions of the same client. `auth` must
stay valid while it is in use; a `static const` structure is simplest.

The new authentication applies to calls started afterwards. Calls already
started keep the credentials they were encoded with; their replies are
checked with `validate`, and refreshed, only while the same
authentication is still set.

Only the credential and verifier are pluggable. Flavors that also
transform the arguments and results (RPCSEC_GSS integrity and privacy) are
not supported; use TLS for privacy.

## RETURN VALUE

`ONCRPC_OK`, or `ONCRPC_ERR_INVALID` if `clnt` or `auth` is `NULL`,
`auth->credential` is `NULL`, or the client is closing.

A call whose `credential` or `verifier` function returns -1 is refused by
`oncRPCClientCall()` with `ONCRPC_ERR_CANTENCODE`. A call whose reply fails
`validate` completes with `ONCRPC_ERR_AUTHERROR`, and
`oncRPCClientLastError(clnt)->why` is `ONCRPC_AUTH_INVALIDRESP`.

## EXAMPLE

A flavor that sends a bearer token, renewed when the server refuses it:

```c
#include <string.h>
#include <oncrpc.h>

#define TOKEN_FLAVOR 390000u   /* agreed with the server */

struct session {
    char token[64];
    void (*renew)(char *token, size_t size);   /* fetches a new token */
};

static int token_cred(void *data, uint32_t xid, uint32_t proc, oncRPCAuthData *cred)
{
    struct session *s = data;
    size_t n = strlen(s->token);
    (void)xid;
    (void)proc;
    cred->flavor = TOKEN_FLAVOR;
    cred->len = (uint32_t)n;
    memcpy(cred->body, s->token, n);
    return 0;
}

static int token_refresh(void *data, oncRPCAuthStat why)
{
    struct session *s = data;
    if (why != ONCRPC_AUTH_REJECTEDCRED)
        return 0;
    s->renew(s->token, sizeof(s->token));
    return 1;   /* send the call again with the new token */
}

static const oncRPCClientAuth token_auth = { token_cred, NULL, NULL, token_refresh, NULL };

oncRPCStat use_token(oncRPCClient *client, struct session *s)
{
    return oncRPCClientSetAuth(client, &token_auth, s);
}
```

`examples/11_auth` in the source tree has a complete client and server
with signed verifiers.

## SEE ALSO

[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md),
[oncRPCSvcCred(3)](oncRPCSvcCred.3.md),
[oncRPCClientSetAuthSys(3)](oncRPCClientSetAuthSys.3.md),
[oncRPCClientLastError(3)](oncRPCClientLastError.3.md)
