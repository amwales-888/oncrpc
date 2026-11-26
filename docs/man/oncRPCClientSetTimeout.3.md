# oncRPCClientSetTimeout(3)

## NAME

oncRPCClientSetTimeout - time allowed for each call

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCClientSetTimeout(oncRPCClient *clnt, uint32_t ms);
```

## DESCRIPTION

`oncRPCClientSetTimeout()` sets how long a call may take, in milliseconds,
counted from [oncRPCClientCall(3)](oncRPCClientCall.3.md) and including
connecting, waiting in the queue and any resending. A call that has not
finished by then completes with `ONCRPC_ERR_TIMEDOUT`. The default is
`ONCRPC_DEFAULT_TIMEOUT_MS` (25 seconds). A value of 0 is treated as 1.

The new timeout applies to calls started afterwards. `NULL` is ignored.

## EXAMPLE

```c
#include <oncrpc.h>

void configure(oncRPCClient *client)
{
    oncRPCClientSetTimeout(client, 5000);   /* give up after 5 s */
}
```

## SEE ALSO

[oncRPCClientSetRetry(3)](oncRPCClientSetRetry.3.md),
[oncRPCClientCancel(3)](oncRPCClientCancel.3.md)
