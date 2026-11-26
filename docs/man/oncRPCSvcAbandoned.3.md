# oncRPCSvcAbandoned(3)

## NAME

oncRPCSvcAbandoned - whether a request's reply can still be delivered

## SYNOPSIS

```c
#include <oncrpc.h>

int oncRPCSvcAbandoned(const oncRPCSvcReq *req);
```

## DESCRIPTION

A request that is answered later can outlive its caller: the TCP or local
connection it came on may close, or the server may start closing. The
request is then abandoned. It stays valid and must still be finished
(dropping it is enough), but a reply would go nowhere.

`oncRPCSvcAbandoned()` tells whether this has happened, so that work
done for the request can be skipped. A UDP request is abandoned only when
the server closes.

To be told when it happens instead of asking, use
[oncRPCSvcSetAbandonCb(3)](oncRPCSvcSetAbandonCb.3.md).

## RETURN VALUE

Non-zero if the request is abandoned (or `req` is `NULL`), 0 otherwise.

## EXAMPLE

A long job processed in steps checks between steps:

```c
#include <oncrpc.h>

struct job {
    oncRPCSvcReq *req;
    int           step;
};

int next_step(struct job *j);   /* returns 0 when the work is done */

void work(struct job *j)
{
    if (oncRPCSvcAbandoned(j->req)) {
        oncRPCSvcDrop(j->req);   /* nobody is waiting any more */
        return;
    }
    if (next_step(j) == 0)
        oncRPCSvcReply(j->req, NULL, NULL);
}
```

## SEE ALSO

[oncRPCSvcSetAbandonCb(3)](oncRPCSvcSetAbandonCb.3.md),
[oncRPCSvcDrop(3)](oncRPCSvcDrop.3.md),
[oncRPCServerClose(3)](oncRPCServerClose.3.md)
