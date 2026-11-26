# oncRPCClientSetRetry(3)

## NAME

oncRPCClientSetRetry - UDP retransmission interval

## SYNOPSIS

```c
#include <oncrpc.h>

void oncRPCClientSetRetry(oncRPCClient *clnt, uint32_t ms);
```

## DESCRIPTION

UDP can lose datagrams, so a UDP client sends a call again if no reply has
arrived after an interval. `oncRPCClientSetRetry()` sets that interval in
milliseconds; the default is `ONCRPC_DEFAULT_RETRY_MS` (1 second). A value
of 0 is treated as 1. Calls are resent until a reply arrives or the
timeout ([oncRPCClientSetTimeout(3)](oncRPCClientSetTimeout.3.md)) ends
them.

The setting has no effect on TCP and local clients, which never resend.
`NULL` is ignored.

A resent call reaches the server again. A liboncrpc server answers a
resend from its duplicate-request cache instead of running the procedure
twice (see [oncRPCServerSetLimits(3)](oncRPCServerSetLimits.3.md)); other
servers may not.

## EXAMPLE

```c
#include <oncrpc.h>

void tune_for_lan(oncRPCClient *client)
{
    oncRPCClientSetRetry(client, 200);      /* resend after 200 ms */
    oncRPCClientSetTimeout(client, 2000);   /* up to about 10 sends */
}
```

## SEE ALSO

[oncRPCClientSetTimeout(3)](oncRPCClientSetTimeout.3.md),
[oncRPCServerSetLimits(3)](oncRPCServerSetLimits.3.md)
