# oncRPCServerGetStats(3)

## NAME

oncRPCServerGetStats - what a server has done

## SYNOPSIS

```c
#include <oncrpc.h>

typedef struct oncRPCServerStats {
    uint64_t requests;
    uint64_t duplicatesReplayed;
    uint64_t duplicatesDropped;
    uint64_t requestsDroppedBusy;
    uint64_t connectionsAccepted;
    uint64_t connectionsRefused;
    uint64_t connectionsIdleClosed;
    uint64_t connectionsPaused;
    size_t   connections;
    size_t   pendingRequests;
    size_t   duplicateCacheEntries;
    size_t   duplicateCacheBytes;
    uint64_t tlsSessions;
    uint64_t tlsFailures;
} oncRPCServerStats;

void oncRPCServerGetStats(const oncRPCServer *srv, oncRPCServerStats *stats);
```

## DESCRIPTION

`oncRPCServerGetStats()` copies the server's counters into `*stats`, for
monitoring. The `uint64_t` fields count since the server was created; the
`size_t` fields describe the present.

| Field | Counts |
|---|---|
| `requests` | requests received, not counting UDP duplicates |
| `duplicatesReplayed` | UDP resends answered from the duplicate-request cache |
| `duplicatesDropped` | UDP resends of requests still being handled |
| `requestsDroppedBusy` | UDP requests dropped because `maxRequests` was reached |
| `connectionsAccepted` | connections accepted |
| `connectionsRefused` | connections closed at once because of `maxConnections` |
| `connectionsIdleClosed` | connections closed by `idleTimeoutMs` |
| `connectionsPaused` | times a connection stopped being read because of a request limit |
| `connections` | connections open now |
| `pendingRequests` | requests being handled now |
| `duplicateCacheEntries`, `duplicateCacheBytes` | contents of the cache now |
| `tlsSessions` | connections upgraded to TLS |
| `tlsFailures` | TLS handshakes that failed |

Nothing happens if either argument is `NULL`.

## EXAMPLE

Log the counters every minute:

```c
#include <stdio.h>
#include <oncrpc.h>

static void on_report(uv_timer_t *t)
{
    oncRPCServerStats st;

    oncRPCServerGetStats(t->data, &st);
    printf("%llu requests, %zu connections, %llu refused, %llu resends replayed\n",
           (unsigned long long)st.requests, st.connections,
           (unsigned long long)st.connectionsRefused,
           (unsigned long long)st.duplicatesReplayed);
}

void report_every_minute(uv_loop_t *loop, uv_timer_t *timer, oncRPCServer *server)
{
    uv_timer_init(loop, timer);
    timer->data = server;
    uv_timer_start(timer, on_report, 60000, 60000);
    uv_unref((uv_handle_t *)timer);   /* do not keep the loop alive */
}
```

## SEE ALSO

[oncRPCServerSetLimits(3)](oncRPCServerSetLimits.3.md),
[oncRPCServerPending(3)](oncRPCServerPending.3.md)
