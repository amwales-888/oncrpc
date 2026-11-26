# oncRPCServerSetLimits(3)

## NAME

oncRPCServerSetLimits - change a server's limits

## SYNOPSIS

```c
#include <oncrpc.h>

typedef struct oncRPCServerLimits {
    size_t   maxConnections;
    size_t   maxRequests;
    size_t   maxRequestsPerConnection;
    uint32_t idleTimeoutMs;
    size_t   maxRecord;
    size_t   duplicateCacheEntries;
    size_t   duplicateCacheBytes;
    uint32_t duplicateCacheTtlMs;
} oncRPCServerLimits;

oncRPCStat oncRPCServerSetLimits(oncRPCServer *srv, const oncRPCServerLimits *limits);
```

## DESCRIPTION

`oncRPCServerSetLimits()` replaces the limits that protect the server from
busy or hostile clients. Changes apply at once. For the count and time
limits, 0 means no limit.

| Field | Default | Meaning |
|---|---|---|
| `maxConnections` | 1024 | TCP and local connections open at once; more are closed as soon as they are accepted |
| `maxRequests` | 4096 | requests being handled at once over all transports; when reached, connections stop being read and new UDP requests are dropped (clients resend) |
| `maxRequestsPerConnection` | 64 | requests being handled at once for one connection; when reached, that connection stops being read until one finishes |
| `idleTimeoutMs` | 300000 | close connections with no traffic and no outstanding request for this long |
| `maxRecord` | 16 MiB | largest request record accepted over TCP; must not be 0 |
| `duplicateCacheEntries` | 1024 | UDP requests remembered by the duplicate-request cache; 0 turns the cache off |
| `duplicateCacheBytes` | 4 MiB | total size of the replies the cache keeps; 0 means no limit |
| `duplicateCacheTtlMs` | 120000 | how long a reply is kept after it was sent; 0 means no limit |

**The duplicate-request cache.** A UDP client that gets no reply sends its
request again; without the cache, the procedure would run a second time.
With it, a resent request is ignored while the original is still being
handled, and answered with the original reply once there is one. The
oldest replies make way for new ones. The cache is emptied when any of its
three limits changes.

Pausing a connection rather than refusing work keeps memory bounded while
letting TCP's own flow control slow the client down.

## RETURN VALUE

`ONCRPC_OK`; `ONCRPC_ERR_INVALID` if an argument is `NULL`, the server is
closing, or `maxRecord` is 0.

## EXAMPLE

A server exposed to the internet, with tighter limits:

```c
#include <oncrpc.h>

oncRPCStat harden(oncRPCServer *server)
{
    oncRPCServerLimits lim;

    oncRPCServerGetLimits(server, &lim);
    lim.maxConnections = 200;
    lim.maxRequestsPerConnection = 8;
    lim.idleTimeoutMs = 30000;          /* 30 s */
    lim.maxRecord = 1024 * 1024;        /* requests are small */
    return oncRPCServerSetLimits(server, &lim);
}
```

## SEE ALSO

[oncRPCServerGetLimits(3)](oncRPCServerGetLimits.3.md),
[oncRPCServerGetStats(3)](oncRPCServerGetStats.3.md),
[oncRPCServerSetMaxRecord(3)](oncRPCServerSetMaxRecord.3.md),
[oncRPCClientSetRetry(3)](oncRPCClientSetRetry.3.md)
