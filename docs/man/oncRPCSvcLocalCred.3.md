# oncRPCSvcLocalCred(3)

## NAME

oncRPCSvcLocalCred - user and group of a local caller

## SYNOPSIS

```c
#include <oncrpc.h>

int oncRPCSvcLocalCred(const oncRPCSvcReq *req, uint32_t *uid, uint32_t *gid);
```

## DESCRIPTION

For a request that arrived over a local socket (`ONCRPC_PROTO_LOCAL`),
`oncRPCSvcLocalCred()` stores the user id and group id of the process at
the other end in `*uid` and `*gid`, as the operating system reports them
when the connection was made. On Linux, macOS and the BSDs these come from
the kernel and cannot be forged by the caller, unlike AUTH_SYS
credentials, which the caller fills in itself.

Windows does not provide them for named pipes.

## RETURN VALUE

0 on success; -1 if the request did not arrive over a local socket, the
ids are not known, or an argument is `NULL`.

## EXAMPLE

Accept calls from the same user as the server only:

```c
#include <oncrpc.h>

int same_user(const oncRPCSvcReq *req, uint32_t my_uid)
{
    uint32_t uid, gid;
    return oncRPCSvcLocalCred(req, &uid, &gid) == 0 && uid == my_uid;
}

void guarded_proc(oncRPCSvcReq *req, uint32_t my_uid)
{
    if (!same_user(req, my_uid)) {
        oncRPCSvcErrAuth(req, ONCRPC_AUTH_TOOWEAK);
        return;
    }
    oncRPCSvcReply(req, NULL, NULL);
}
```

## SEE ALSO

[oncRPCServerListen(3)](oncRPCServerListen.3.md),
[oncRPCSvcPeer(3)](oncRPCSvcPeer.3.md),
[oncRPCServerSetAuth(3)](oncRPCServerSetAuth.3.md)
