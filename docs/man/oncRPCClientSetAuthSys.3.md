# oncRPCClientSetAuthSys(3)

## NAME

oncRPCClientSetAuthSys - send calls with AUTH_SYS credentials

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCClientSetAuthSys(oncRPCClient *clnt, const char *machinename,
                                  uint32_t uid, uint32_t gid, uint32_t ngids,
                                  const uint32_t *gids);
```

## DESCRIPTION

`oncRPCClientSetAuthSys()` makes the client send AUTH_SYS credentials
with the calls it starts from now on: a machine name, a user id, a group
id and up to `ONCRPC_MAX_GIDS` (16) further group ids, from `gids`. The
machine name may be `NULL` (sent empty) and is cut to
`ONCRPC_MAX_MACHINENAME` (255) bytes. A custom authentication set before
is released.

AUTH_SYS is a statement, not a proof: the server has to trust the client
to tell the truth, as on a private network of machines administered
together. Over TLS, or over a local socket where the server can ask the
operating system who the caller is, it can be trusted more.

To send the identity of the running process, use
[oncRPCClientSetAuthSysDefault(3)](oncRPCClientSetAuthSysDefault.3.md).

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if `clnt` is `NULL`, `ngids` is above
`ONCRPC_MAX_GIDS`, or `gids` is `NULL` while `ngids` is not 0.

## EXAMPLE

```c
#include <oncrpc.h>

oncRPCStat act_as_backup_operator(oncRPCClient *client)
{
    static const uint32_t groups[] = { 34, 100 };
    return oncRPCClientSetAuthSys(client, "backup01", 34, 34, 2, groups);
}
```

## SEE ALSO

[oncRPCClientSetAuthSysDefault(3)](oncRPCClientSetAuthSysDefault.3.md),
[oncRPCClientSetAuthNone(3)](oncRPCClientSetAuthNone.3.md),
[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md)
