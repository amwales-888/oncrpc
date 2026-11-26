# oncRPCSvcPeer(3)

## NAME

oncRPCSvcPeer - describe who sent a request

## SYNOPSIS

```c
#include <oncrpc.h>

int oncRPCSvcPeer(const oncRPCSvcReq *req, char *buf, size_t len);
```

## DESCRIPTION

`oncRPCSvcPeer()` writes the address of the request's sender into `buf`,
as a NUL-terminated string of at most `len` bytes:

- IPv4: `"192.0.2.7:51234"`
- IPv6: `"[2001:db8::7]:51234"`
- a local socket: `"local"`, or `"local (uid 1000, gid 1000)"` when the
  operating system reports the caller's ids (see
  [oncRPCSvcLocalCred(3)](oncRPCSvcLocalCred.3.md)).

The text is meant for logs. A longer description is cut to fit.

## RETURN VALUE

0 on success, -1 if an argument is `NULL`, `len` is 0, or the address is
unknown.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void log_request(const oncRPCSvcReq *req)
{
    char who[80];

    if (oncRPCSvcPeer(req, who, sizeof(who)) != 0)
        snprintf(who, sizeof(who), "unknown");
    printf("%s called procedure %u\n", who, (unsigned)req->proc);
}
```

## SEE ALSO

[oncRPCSvcLocalCred(3)](oncRPCSvcLocalCred.3.md),
[oncRPCSvcTlsPeer(3)](oncRPCSvcTlsPeer.3.md)
