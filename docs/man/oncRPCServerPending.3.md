# oncRPCServerPending(3)

## NAME

oncRPCServerPending - requests not yet finished

## SYNOPSIS

```c
#include <oncrpc.h>

size_t oncRPCServerPending(const oncRPCServer *srv);
```

## DESCRIPTION

`oncRPCServerPending()` returns how many requests have been handed to
dispatch routines and not yet finished with a reply, an error reply or a
drop. A server that answers later uses it to see how much work is in
progress; a closing server waits for it to reach 0.

## RETURN VALUE

The number of requests, or 0 if `srv` is `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void report_backlog(const oncRPCServer *server)
{
    size_t n = oncRPCServerPending(server);
    if (n > 100)
        fprintf(stderr, "warning: %zu requests waiting for an answer\n", n);
}
```

## SEE ALSO

[oncRPCServerGetStats(3)](oncRPCServerGetStats.3.md),
[oncRPCServerClose(3)](oncRPCServerClose.3.md)
