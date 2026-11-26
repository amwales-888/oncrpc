# oncRPCClientSetAuthSysDefault(3)

## NAME

oncRPCClientSetAuthSysDefault - send calls as the current process

## SYNOPSIS

```c
#include <oncrpc.h>

oncRPCStat oncRPCClientSetAuthSysDefault(oncRPCClient *clnt);
```

## DESCRIPTION

`oncRPCClientSetAuthSysDefault()` makes the client send AUTH_SYS
credentials describing the running process: this computer's host name,
and the process's user id, group id and supplementary groups (as many as
fit in `ONCRPC_MAX_GIDS`). It is
[oncRPCClientSetAuthSys(3)](oncRPCClientSetAuthSys.3.md) with the values
filled in.

On Windows, which has no numeric user and group ids, the ids are sent as
0.

## RETURN VALUE

`ONCRPC_OK`, or `ONCRPC_ERR_INVALID` if `clnt` is `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void identify(oncRPCClient *client)
{
    if (oncRPCClientSetAuthSysDefault(client) != ONCRPC_OK)
        fprintf(stderr, "cannot set credentials\n");
}
```

## SEE ALSO

[oncRPCClientSetAuthSys(3)](oncRPCClientSetAuthSys.3.md)
