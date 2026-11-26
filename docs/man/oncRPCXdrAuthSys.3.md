# oncRPCXdrAuthSys(3)

## NAME

oncRPCXdrAuthSys - XDR routine for an AUTH_SYS credential

## SYNOPSIS

```c
#include <oncrpc.h>

int oncRPCXdrAuthSys(oncRPCXdr *xdrs, oncRPCAuthSysParams *p);
```

## DESCRIPTION

`oncRPCXdrAuthSys()` encodes, decodes or frees the body of an AUTH_SYS
credential (RFC 5531, appendix A) held in `p`:

```c
typedef struct oncRPCAuthSysParams {
    uint32_t stamp;                                /* arbitrary id */
    char     machinename[ONCRPC_MAX_MACHINENAME + 1];
    uint32_t uid;
    uint32_t gid;
    uint32_t ngids;                                /* at most ONCRPC_MAX_GIDS */
    uint32_t gids[ONCRPC_MAX_GIDS];
} oncRPCAuthSysParams;
```

The structure holds everything inline, so decoding allocates nothing and
the stream may have no allocator.

Applications rarely need this routine: clients send AUTH_SYS with
[oncRPCClientSetAuthSys(3)](oncRPCClientSetAuthSys.3.md), and servers find
the decoded credential in `req->authSys`. It is useful for a custom
authenticator or a custom flavor that embeds an AUTH_SYS body.

## RETURN VALUE

Non-zero on success; 0 if the stream fails, the machine name is longer than
`ONCRPC_MAX_MACHINENAME` bytes, or there are more than `ONCRPC_MAX_GIDS`
group ids.

## EXAMPLE

Decode the AUTH_SYS body carried by a request's credential:

```c
#include <oncrpc.h>

int caller_uid(oncRPCSvcReq *req, uint32_t *uid)
{
    const oncRPCAuthData *cred = oncRPCSvcCred(req);
    oncRPCAuthSysParams p;
    oncRPCXdr x;

    if (cred->flavor != ONCRPC_AUTH_SYS)
        return -1;
    oncRPCXdrInitDecode(&x, NULL, cred->body, cred->len);
    if (!oncRPCXdrAuthSys(&x, &p))
        return -1;
    *uid = p.uid;
    return 0;
}
```

## SEE ALSO

[oncRPCClientSetAuthSys(3)](oncRPCClientSetAuthSys.3.md),
[oncRPCSvcCred(3)](oncRPCSvcCred.3.md),
[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md)
