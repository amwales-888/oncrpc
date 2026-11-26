# oncRPCClientLastError(3)

## NAME

oncRPCClientLastError - details of the last completed call

## SYNOPSIS

```c
#include <oncrpc.h>

typedef struct oncRPCError {
    oncRPCStat     stat;
    uint32_t       low, high;
    oncRPCAuthStat why;
    int            uvError;
    const char    *detail;
} oncRPCError;

const oncRPCError *oncRPCClientLastError(const oncRPCClient *clnt);
```

## DESCRIPTION

`oncRPCClientLastError()` returns details of the call (or ready
notification) that completed most recently on `clnt`. Read it inside the
call's callback: the next completion overwrites it.

- `stat`: the status the callback received.
- `low`, `high`: for `ONCRPC_ERR_VERSMISMATCH` and
  `ONCRPC_ERR_PROGVERSMISMATCH`, the lowest and highest versions the
  server supports.
- `why`: for `ONCRPC_ERR_AUTHERROR`, the reason the credentials were
  refused, such as `ONCRPC_AUTH_TOOWEAK` or `ONCRPC_AUTH_REJECTEDCRED`;
  `ONCRPC_AUTH_INVALIDRESP` if the reply's verifier failed the client's own
  check.
- `uvError`: for a transport failure (`ONCRPC_ERR_CANTCONNECT`,
  `ONCRPC_ERR_CANTRECV`, ...), the libuv error code behind it, which
  `uv_strerror()` describes; 0 otherwise.
- `detail`: for `ONCRPC_ERR_TLS`, a description of what went wrong (for
  example a certificate that did not check out); `NULL` otherwise.

## RETURN VALUE

A pointer into the client, valid until the client is closed; `NULL` if
`clnt` is `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void report(oncRPCClient *client, oncRPCStat status, void *res, void *data)
{
    const oncRPCError *e = oncRPCClientLastError(client);
    (void)res;
    (void)data;

    switch (status) {
    case ONCRPC_OK:
        break;
    case ONCRPC_ERR_PROGVERSMISMATCH:
        fprintf(stderr, "server supports versions %u to %u only\n",
                (unsigned)e->low, (unsigned)e->high);
        break;
    case ONCRPC_ERR_AUTHERROR:
        fprintf(stderr, "credentials refused (reason %d)\n", (int)e->why);
        break;
    case ONCRPC_ERR_TLS:
        fprintf(stderr, "TLS: %s\n", e->detail ? e->detail : "failed");
        break;
    default:
        if (e->uvError != 0)
            fprintf(stderr, "%s: %s\n", oncRPCStrError(status), uv_strerror(e->uvError));
        else
            fprintf(stderr, "%s\n", oncRPCStrError(status));
    }
}
```

## SEE ALSO

[oncRPCStrError(3)](oncRPCStrError.3.md),
[oncRPCClientCall(3)](oncRPCClientCall.3.md)
