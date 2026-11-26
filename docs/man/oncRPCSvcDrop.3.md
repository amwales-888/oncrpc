# oncRPCSvcDrop(3)

## NAME

oncRPCSvcDrop - finish a request without replying

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCSvcDrop(oncRPCSvcReq *req);
```

## DESCRIPTION

`oncRPCSvcDrop()` finishes `req` without sending anything. Its arguments
are released and it no longer counts as outstanding. A `NULL` request,
or one already finished, is ignored.

The caller then sees no answer: a TCP client waits until its timeout, a
UDP client resends. Use it:

- for requests whose caller has gone (see
  [oncRPCSvcSetAbandonCb(3)](oncRPCSvcSetAbandonCb.3.md)), where a reply
  would go nowhere anyway;
- for procedures that by design have no reply, such as one-way messages
  and batched calls;
- to ignore traffic that does not deserve an answer.

Once finished, a request may be freed at once: do not use `req` again.

## EXAMPLE

A one-way "log this" procedure:

```c
#include <stdio.h>
#include <oncrpc.h>

static int xdr_text(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrString(xdrs, (char **)obj, 1024);
}

void log_proc(oncRPCSvcReq *req)
{
    char **text = oncRPCSvcDecodeArgs(req, xdr_text, sizeof(char *));
    if (text != NULL)
        printf("remote log: %s\n", *text);
    oncRPCSvcDrop(req);   /* the client does not wait for an answer */
}
```

## SEE ALSO

[oncRPCSvcReply(3)](oncRPCSvcReply.3.md),
[oncRPCSvcAbandoned(3)](oncRPCSvcAbandoned.3.md)
