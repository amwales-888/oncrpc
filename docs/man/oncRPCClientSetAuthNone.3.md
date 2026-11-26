# oncRPCClientSetAuthNone(3)

## NAME

oncRPCClientSetAuthNone - send calls without credentials

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCClientSetAuthNone(oncRPCClient *clnt);
```

## DESCRIPTION

`oncRPCClientSetAuthNone()` makes the client send AUTH_NONE (no
credentials) with the calls it starts from now on. This is the default for
a new client, so the function is only needed to go back to it after
[oncRPCClientSetAuthSys(3)](oncRPCClientSetAuthSys.3.md) or
[oncRPCClientSetAuth(3)](oncRPCClientSetAuth.3.md). A custom
authentication set before is released.

Calls already started keep the credentials they were sent with.

## RETURN VALUE

`ONCRPC_OK`, or `ONCRPC_ERR_INVALID` if `clnt` is `NULL`.

## EXAMPLE

```c
#include <oncrpc.h>

/* Ask for public data anonymously, then switch to the user's identity. */
void anonymous_then_user(oncRPCClient *client, uint32_t uid, uint32_t gid)
{
    oncRPCClientSetAuthNone(client);
    oncRPCClientCall(client, 1, NULL, NULL, NULL, NULL, NULL, NULL);   /* sent anonymously */
    oncRPCClientSetAuthSys(client, "workstation", uid, gid, 0, NULL);
    oncRPCClientCall(client, 2, NULL, NULL, NULL, NULL, NULL, NULL);   /* sent as uid */
}
```

## SEE ALSO

[oncRPCClientSetAuthSys(3)](oncRPCClientSetAuthSys.3.md),
[oncRPCClientSetAuth(3)](oncRPCClientSetAuth.3.md)
