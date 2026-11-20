/*
 * test_soak.c - many clients against one server with small limits, UDP
 * traffic passing through a relay that loses, duplicates and reorders
 * datagrams. Half of the stream clients use TCP, the other half a local
 * socket (a pipe on Windows).
 *
 * Every call carries a unique id; the server counts how often each id runs.
 * The test checks that:
 *
 *  - with a duplicate-request cache large enough to hold every request, no
 *    UDP call runs more than once, however its datagrams are mangled;
 *  - no TCP call runs more than once (TCP clients never resend);
 *  - every call either returns the right answer or fails in one of the ways
 *    the conditions allow (UDP: timed out; TCP: connection closed by the
 *    idle timeout);
 *  - the cache never holds more than its limits allow;
 *  - the server's counters agree with what the procedures saw;
 *  - everything shuts down with no memory left allocated.
 *
 * Usage: test_soak [seed [scale]]
 *   seed   start of the random sequence (default 1). The relay's decisions
 *          and the clients' choices follow it, but which datagram meets
 *          which decision also depends on timing, so a seed narrows a
 *          failure down rather than replaying it exactly.
 *   scale  multiplies the number of calls (default 1, about 10 seconds).
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calc.h"

static int g_fail, g_count;

#define CHECK(cond) do { g_count++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- Counting allocator ---------------------------------------------------------- */

static long g_live;

static void *t_realloc(void *ctx, void *p, size_t n)
{
    void *q;
    (void)ctx;
    q = realloc(p, n);
    if (p == NULL && q != NULL)
        g_live++;
    return q;
}

static void t_free(void *ctx, void *p)
{
    (void)ctx;
    if (p != NULL)
        g_live--;
    free(p);
}

static const oncRPCAllocator A = { t_realloc, t_free, NULL };

static uv_loop_t g_loop;

#define WAIT(cond) do { while (!(cond)) uv_run(&g_loop, UV_RUN_ONCE); } while (0)

/* ---- Random numbers (xorshift32: the same sequence on every platform) ----------- */

static uint32_t g_rng = 1;

static uint32_t rnd(void)
{
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g_rng = x;
}

/* 1 with probability percent/100. */
static int chance(unsigned percent)
{
    return rnd() % 100u < percent;
}

/* ---- Server ------------------------------------------------------------------------ */

/* CALC_ADD(id, delay) answers id + delay after delay milliseconds. */
static uint16_t *g_runs;      /* times each id ran */
static size_t    g_maxIds;
static uint64_t  g_totalRuns;
static size_t    g_slowOpen;  /* deferred replies not yet sent */

typedef struct slow {
    uv_timer_t    t;
    oncRPCSvcReq *req;
    int32_t       sum;
} slow;

static void on_slow_closed(uv_handle_t *h)
{
    free(h->data);
    g_slowOpen--;
}

static void on_slow(uv_timer_t *t)
{
    slow *w = (slow *)t->data;
    (void)oncRPCCalcAddV1Reply(w->req, &w->sum);
    uv_close((uv_handle_t *)t, on_slow_closed);
}

void oncRPCCalcAddV1Svc(const int32_t *a, const int32_t *b, oncRPCSvcReq *req)
{
    int32_t sum = *a + *b;
    slow *w;

    if (*a >= 0 && (size_t)*a < g_maxIds)
        g_runs[*a]++;
    g_totalRuns++;
    if (*b <= 0) {
        (void)oncRPCCalcAddV1Reply(req, &sum);
        return;
    }
    w = (slow *)malloc(sizeof(*w));
    if (w == NULL) {
        oncRPCSvcErrSystem(req);
        return;
    }
    w->req = req;
    w->sum = sum;
    w->t.data = w;
    uv_timer_init(&g_loop, &w->t);
    uv_timer_start(&w->t, on_slow, (uint64_t)*b, 0);
    g_slowOpen++;
}

/* The other procedures are not used here. */
void oncRPCCalcNullV1Svc(oncRPCSvcReq *req) { oncRPCSvcErrNoProc(req); }
void oncRPCCalcComputeV1Svc(const calc_args *arg, oncRPCSvcReq *req) { (void)arg; oncRPCSvcErrNoProc(req); }
void oncRPCCalcStatsV1Svc(const value_list *arg, oncRPCSvcReq *req) { (void)arg; oncRPCSvcErrNoProc(req); }
void oncRPCCalcEchoV1Svc(const char *arg, oncRPCSvcReq *req) { (void)arg; oncRPCSvcErrNoProc(req); }
void oncRPCCalcReverseV1Svc(const name_list *arg, oncRPCSvcReq *req) { (void)arg; oncRPCSvcErrNoProc(req); }

/* ---- Lossy UDP relay --------------------------------------------------------------- */

/*
 * Clients send to the relay's front socket. For each client the relay keeps
 * its own socket towards the server, so the server sees one stable address
 * per client, and replies coming back on it go to that client.
 */
#define MAX_PEERS 128

typedef struct rpeer {
    struct sockaddr_in addr;   /* the client */
    uv_udp_t           up;     /* towards the server */
} rpeer;

typedef struct rpacket {
    uv_timer_t         t;
    uv_udp_t          *via;
    struct sockaddr_in to;
    size_t             len;
    uint8_t            data[1];
} rpacket;

static uv_udp_t            g_front;
static struct sockaddr_in  g_serverAddr;
static rpeer               g_peers[MAX_PEERS];
static int                 g_npeers;
static size_t              g_delayed;     /* packets waiting on a timer */
static size_t              g_relayHandles;
static unsigned            g_dropPct, g_dupPct, g_delayPct;
static uint64_t            g_relayed, g_dropped, g_duplicated, g_reordered;
static uint8_t             g_rbuf[65536];

static void relay_alloc(uv_handle_t *h, size_t suggested, uv_buf_t *b)
{
    (void)h;
    (void)suggested;
    *b = uv_buf_init((char *)g_rbuf, (unsigned int)sizeof(g_rbuf));
}

static void send_now(uv_udp_t *via, const struct sockaddr_in *to, const uint8_t *p, size_t len)
{
    uv_buf_t b = uv_buf_init((char *)p, (unsigned int)len);
    /* A full socket buffer is just one more lost datagram. */
    (void)uv_udp_try_send(via, &b, 1, (const struct sockaddr *)to);
}

static void on_packet_closed(uv_handle_t *h)
{
    free(h->data);
    g_delayed--;
}

static void on_packet(uv_timer_t *t)
{
    rpacket *p = (rpacket *)t->data;
    send_now(p->via, &p->to, p->data, p->len);
    uv_close((uv_handle_t *)t, on_packet_closed);
}

static void send_later(uv_udp_t *via, const struct sockaddr_in *to, const uint8_t *data,
                       size_t len, uint64_t ms)
{
    rpacket *p = (rpacket *)malloc(sizeof(*p) + len);
    if (p == NULL)
        return;
    p->via = via;
    p->to = *to;
    p->len = len;
    memcpy(p->data, data, len);
    p->t.data = p;
    uv_timer_init(&g_loop, &p->t);
    uv_timer_start(&p->t, on_packet, ms, 0);
    g_delayed++;
}

/* Pass a datagram on, perhaps losing, duplicating or delaying it. */
static void forward(uv_udp_t *via, const struct sockaddr_in *to, const uint8_t *data, size_t len)
{
    int copies = 1, i;

    g_relayed++;
    if (chance(g_dropPct)) {
        g_dropped++;
        return;
    }
    if (chance(g_dupPct)) {
        g_duplicated++;
        copies = 2;
    }
    for (i = 0; i < copies; i++) {
        if (chance(g_delayPct)) {
            g_reordered++;
            send_later(via, to, data, len, 1 + rnd() % 30u);
        } else {
            send_now(via, to, data, len);
        }
    }
}

static void on_up_recv(uv_udp_t *u, ssize_t n, const uv_buf_t *b, const struct sockaddr *from,
                       unsigned flags)
{
    rpeer *p = (rpeer *)u->data;
    (void)from;
    (void)flags;
    if (n > 0)
        forward(&g_front, &p->addr, (const uint8_t *)b->base, (size_t)n);
}

static void on_front_recv(uv_udp_t *u, ssize_t n, const uv_buf_t *b, const struct sockaddr *from,
                          unsigned flags)
{
    const struct sockaddr_in *f = (const struct sockaddr_in *)(const void *)from;
    struct sockaddr_in any;
    rpeer *p = NULL;
    int i;
    (void)u;
    (void)flags;

    if (n <= 0 || from == NULL || from->sa_family != AF_INET)
        return;
    for (i = 0; i < g_npeers; i++)
        if (g_peers[i].addr.sin_port == f->sin_port &&
            g_peers[i].addr.sin_addr.s_addr == f->sin_addr.s_addr) {
            p = &g_peers[i];
            break;
        }
    if (p == NULL) {
        if (g_npeers == MAX_PEERS)
            return;
        p = &g_peers[g_npeers++];
        p->addr = *f;
        uv_udp_init(&g_loop, &p->up);
        p->up.data = p;
        g_relayHandles++;
        uv_ip4_addr("127.0.0.1", 0, &any);
        if (uv_udp_bind(&p->up, (const struct sockaddr *)&any, 0) != 0 ||
            uv_udp_recv_start(&p->up, relay_alloc, on_up_recv) != 0) {
            fprintf(stderr, "relay: cannot open a socket\n");
            abort();
        }
    }
    forward(&p->up, &g_serverAddr, (const uint8_t *)b->base, (size_t)n);
}

static uint16_t relay_open(uint16_t serverPort)
{
    struct sockaddr_storage name;
    int len = (int)sizeof(name);
    struct sockaddr_in any;

    g_npeers = 0;
    uv_ip4_addr("127.0.0.1", serverPort, &g_serverAddr);
    uv_ip4_addr("127.0.0.1", 0, &any);
    uv_udp_init(&g_loop, &g_front);
    g_relayHandles = 1;
    CHECK(uv_udp_bind(&g_front, (const struct sockaddr *)&any, 0) == 0);
    CHECK(uv_udp_recv_start(&g_front, relay_alloc, on_front_recv) == 0);
    CHECK(uv_udp_getsockname(&g_front, (struct sockaddr *)&name, &len) == 0);
    {
        /* The port is in network byte order. */
        const uint8_t *p = (const uint8_t *)&((struct sockaddr_in *)&name)->sin_port;
        return (uint16_t)((p[0] << 8) | p[1]);
    }
}

static void on_relay_closed(uv_handle_t *h)
{
    (void)h;
    g_relayHandles--;
}

static void relay_close(void)
{
    int i;
    WAIT(g_delayed == 0);
    uv_close((uv_handle_t *)&g_front, on_relay_closed);
    for (i = 0; i < g_npeers; i++)
        uv_close((uv_handle_t *)&g_peers[i].up, on_relay_closed);
    WAIT(g_relayHandles == 0);
}

/* ---- Clients ------------------------------------------------------------------------ */

typedef struct sclient sclient;

typedef struct scall {
    sclient *cl;
    int32_t  id;
    int32_t  delay;
    int32_t  sum;
} scall;

struct sclient {
    oncRPCClient *c;
    oncRPCProto   proto;
    int           toIssue;      /* calls still to start */
    int           outstanding;
    int           window;       /* calls kept outstanding */
    int           resting;      /* waiting before the next burst */
    uv_timer_t    rest;
    int           restOpen;
};

typedef struct tally {
    uint64_t ok, wrong, timedOut, connLost, otherErr, notStarted;
} tally;

static tally   g_udp, g_tcp;
static int32_t g_nextId;
static int     g_clientsOpen;
static int     g_restPct;
static unsigned g_slowPct;

static void refill(sclient *s);

static void on_client_closed(void *data)
{
    (void)data;
    g_clientsOpen--;
}

static void on_rest_closed(uv_handle_t *h)
{
    sclient *s = (sclient *)h->data;
    s->restOpen = 0;
}

static void maybe_finish(sclient *s)
{
    if (s->toIssue > 0 || s->outstanding > 0 || s->c == NULL)
        return;
    oncRPCClientClose(s->c, on_client_closed, NULL);
    s->c = NULL;
    if (s->restOpen)
        uv_close((uv_handle_t *)&s->rest, on_rest_closed);
}

static void on_rest(uv_timer_t *t)
{
    sclient *s = (sclient *)t->data;
    s->resting = 0;
    refill(s);
    maybe_finish(s);
}

static void on_add(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    scall *k = (scall *)data;
    sclient *s = k->cl;
    tally *t = s->proto == ONCRPC_PROTO_UDP ? &g_udp : &g_tcp;
    (void)c;
    (void)res;

    if (st == ONCRPC_OK) {
        if (k->sum == k->id + k->delay) {
            t->ok++;
        } else {
            t->wrong++;
            fprintf(stderr, "call %ld: got %ld, expected %ld\n", (long)k->id, (long)k->sum,
                    (long)(k->id + k->delay));
        }
    } else if (st == ONCRPC_ERR_TIMEDOUT) {
        t->timedOut++;
    } else if (st == ONCRPC_ERR_CANTRECV || st == ONCRPC_ERR_CANTSEND) {
        t->connLost++;
    } else {
        t->otherErr++;
        fprintf(stderr, "call %ld: %s\n", (long)k->id, oncRPCStrError(st));
    }
    free(k);
    s->outstanding--;
    /* TCP clients sometimes go quiet for longer than the idle timeout. */
    if (s->proto != ONCRPC_PROTO_UDP && !s->resting && s->toIssue > 0 && chance((unsigned)g_restPct))
        s->resting = 1;
    if (s->resting && s->outstanding == 0) {
        uv_timer_start(&s->rest, on_rest, 60 + rnd() % 60u, 0);
        return;
    }
    refill(s);
    maybe_finish(s);
}

static void refill(sclient *s)
{
    while (!s->resting && s->toIssue > 0 && s->outstanding < s->window) {
        scall *k = (scall *)malloc(sizeof(*k));
        tally *t = s->proto == ONCRPC_PROTO_UDP ? &g_udp : &g_tcp;
        if (k == NULL)
            abort();
        k->cl = s;
        k->id = g_nextId++;
        k->delay = chance(g_slowPct) ? (int32_t)(20 + rnd() % 100u) : 0;
        k->sum = 0;
        s->toIssue--;
        if (oncRPCCalcAddV1(s->c, &k->id, &k->delay, &k->sum, on_add, k) != ONCRPC_OK) {
            t->notStarted++;
            free(k);
            continue;
        }
        s->outstanding++;
    }
}

/* ---- One run --------------------------------------------------------------------------- */

typedef struct phase {
    const char        *name;
    oncRPCServerLimits lim;
    int                udpClients, tcpClients, callsPerClient;
    int                strictOnce;   /* the cache must hold every UDP request */
} phase;

static oncRPCServer      *g_srv;
static char               g_localPath[256];   /* the server's local socket */
static oncRPCServerLimits g_lim;
static uint64_t           g_samples, g_overEntries, g_overBytes;

/* Check the cache against its limits every few milliseconds. */
static void on_sample(uv_timer_t *t)
{
    oncRPCServerStats st;
    (void)t;
    oncRPCServerGetStats(g_srv, &st);
    g_samples++;
    if (st.duplicateCacheEntries > g_lim.duplicateCacheEntries)
        g_overEntries++;
    if (g_lim.duplicateCacheBytes > 0 && st.duplicateCacheBytes > g_lim.duplicateCacheBytes)
        g_overBytes++;
}

static void on_server_closed(void *data)
{
    *(int *)data = 1;
}

static void guard(uv_timer_t *t)
{
    (void)t;
    fprintf(stderr, "soak phase did not finish within its time limit\n");
    abort();
}

static void run_phase(const phase *ph)
{
    oncRPCServerStats st;
    sclient *cl;
    uv_timer_t sample, watchdog;
    uint16_t port, relayPort;
    int n = ph->udpClients + ph->tcpClients, i, closed = 0;
    size_t id, ranTwice = 0, maxRuns = 0;
    uint64_t udpCalls, tcpCalls;

    memset(&g_udp, 0, sizeof(g_udp));
    memset(&g_tcp, 0, sizeof(g_tcp));
    g_relayed = g_dropped = g_duplicated = g_reordered = 0;
    g_samples = g_overEntries = g_overBytes = 0;
    g_totalRuns = 0;
    g_nextId = 0;
    g_maxIds = (size_t)n * (size_t)ph->callsPerClient;
    g_runs = (uint16_t *)calloc(g_maxIds, sizeof(*g_runs));
    cl = (sclient *)calloc((size_t)n, sizeof(*cl));
    if (g_runs == NULL || cl == NULL)
        abort();

    CHECK(oncRPCServerCreate(&g_loop, &A, &g_srv) == ONCRPC_OK);
    CHECK(oncRPCServerListen(g_srv, ONCRPC_PROTO_TCP, "127.0.0.1", 0) == ONCRPC_OK);
    CHECK(oncRPCServerListen(g_srv, ONCRPC_PROTO_UDP, "127.0.0.1", 0) == ONCRPC_OK);
    CHECK(oncRPCServerListen(g_srv, ONCRPC_PROTO_LOCAL, g_localPath, 0) == ONCRPC_OK);
    CHECK(oncRPCCalcProgV1Register(g_srv, NULL) == ONCRPC_OK);
    CHECK(oncRPCServerSetLimits(g_srv, &ph->lim) == ONCRPC_OK);
    g_lim = ph->lim;
    port = oncRPCServerPort(g_srv, ONCRPC_PROTO_TCP);
    relayPort = relay_open(oncRPCServerPort(g_srv, ONCRPC_PROTO_UDP));

    uv_timer_init(&g_loop, &sample);
    uv_timer_start(&sample, on_sample, 5, 5);
    uv_timer_init(&g_loop, &watchdog);
    uv_timer_start(&watchdog, guard, 300000, 0);

    g_clientsOpen = 0;
    for (i = 0; i < n; i++) {
        sclient *s = &cl[i];
        s->proto = i < ph->udpClients ? ONCRPC_PROTO_UDP
                   : (i - ph->udpClients) % 2 ? ONCRPC_PROTO_LOCAL : ONCRPC_PROTO_TCP;
        s->toIssue = ph->callsPerClient;
        s->window = s->proto == ONCRPC_PROTO_UDP ? 4 : 8;
        uv_timer_init(&g_loop, &s->rest);
        s->rest.data = s;
        s->restOpen = 1;
        CHECK(oncRPCClientCreate(&g_loop, &A,
                                 s->proto == ONCRPC_PROTO_LOCAL ? g_localPath : "127.0.0.1",
                                 s->proto == ONCRPC_PROTO_UDP ? relayPort : port, CALC_PROG,
                                 CALC_VERS, s->proto, NULL, s, &s->c) == ONCRPC_OK);
        if (s->proto == ONCRPC_PROTO_UDP) {
            /* Resend quickly so that resends often meet requests still in
             * hand, and give up late so that losses rarely fail a call. */
            oncRPCClientSetRetry(s->c, 40);
            oncRPCClientSetTimeout(s->c, 5000);
        } else {
            oncRPCClientSetTimeout(s->c, 20000);
        }
        g_clientsOpen++;
    }
    for (i = 0; i < n; i++)
        refill(&cl[i]);

    WAIT(g_clientsOpen == 0);
    for (i = 0; i < n; i++)
        if (cl[i].restOpen)
            uv_close((uv_handle_t *)&cl[i].rest, on_rest_closed);
    WAIT(g_slowOpen == 0);
    oncRPCServerGetStats(g_srv, &st);
    uv_close((uv_handle_t *)&sample, NULL);
    uv_close((uv_handle_t *)&watchdog, NULL);
    relay_close();

    for (id = 0; id < g_maxIds; id++) {
        if (g_runs[id] > 1)
            ranTwice++;
        if (g_runs[id] > maxRuns)
            maxRuns = g_runs[id];
    }
    udpCalls = (uint64_t)ph->udpClients * (uint64_t)ph->callsPerClient;
    tcpCalls = (uint64_t)ph->tcpClients * (uint64_t)ph->callsPerClient;

    printf("  %s\n", ph->name);
    printf("    UDP: %llu calls, %llu ok, %llu timed out; relay %llu datagrams, %llu lost, "
           "%llu duplicated, %llu delayed\n",
           (unsigned long long)udpCalls, (unsigned long long)g_udp.ok,
           (unsigned long long)g_udp.timedOut, (unsigned long long)g_relayed,
           (unsigned long long)g_dropped, (unsigned long long)g_duplicated,
           (unsigned long long)g_reordered);
    printf("    TCP and local: %llu calls, %llu ok, %llu lost to idle closes\n",
           (unsigned long long)tcpCalls, (unsigned long long)g_tcp.ok,
           (unsigned long long)g_tcp.connLost);
    printf("    server: %llu requests, %llu resends replayed, %llu resends ignored, "
           "%llu dropped busy, %llu pauses, %llu idle closes\n",
           (unsigned long long)st.requests, (unsigned long long)st.duplicatesReplayed,
           (unsigned long long)st.duplicatesDropped, (unsigned long long)st.requestsDroppedBusy,
           (unsigned long long)st.connectionsPaused, (unsigned long long)st.connectionsIdleClosed);
    printf("    calls run more than once: %lu (most runs of one call: %lu)\n",
           (unsigned long)ranTwice, (unsigned long)maxRuns);

    /* Every call accounted for, with the right answer when it succeeded. */
    CHECK(g_udp.ok + g_udp.wrong + g_udp.timedOut + g_udp.connLost + g_udp.otherErr +
              g_udp.notStarted == udpCalls);
    CHECK(g_tcp.ok + g_tcp.wrong + g_tcp.timedOut + g_tcp.connLost + g_tcp.otherErr +
              g_tcp.notStarted == tcpCalls);
    CHECK(g_udp.wrong == 0 && g_tcp.wrong == 0);
    CHECK(g_udp.otherErr == 0 && g_tcp.otherErr == 0);
    CHECK(g_udp.notStarted == 0 && g_tcp.notStarted == 0);
    CHECK(g_udp.connLost == 0);   /* UDP has no connection to lose */
    CHECK(g_tcp.timedOut == 0);
    /* Losses must not defeat the clients' resends. */
    CHECK(g_udp.ok * 100 >= udpCalls * 99);
    /* Only the idle timeout may cut a TCP call short: a client can send its
     * next calls just as the server closes the quiet connection. Each such
     * close costs at most the calls that client had in flight (its window
     * of 8). How often it happens depends on the machine's speed. */
    CHECK(g_tcp.connLost <= 8 * st.connectionsIdleClosed);

    /* The server's view agrees with the procedures'. */
    CHECK(st.requests == g_totalRuns);
    CHECK(st.pendingRequests == 0);
    CHECK(g_samples > 0 && g_overEntries == 0 && g_overBytes == 0);

    /* The conditions really were exercised. */
    CHECK(g_dropped > 0 && g_duplicated > 0 && g_reordered > 0);
    CHECK(st.duplicatesReplayed > 0);
    CHECK(st.connectionsPaused > 0);
    if (ph->lim.idleTimeoutMs > 0)
        CHECK(st.connectionsIdleClosed > 0);

    if (ph->strictOnce) {
        CHECK(ranTwice == 0);
        CHECK(st.duplicatesDropped > 0);
    } else {
        /* A small cache forgets; calls may run again, but the answers above
         * were still right. */
        CHECK(maxRuns >= 1);
    }

    oncRPCServerClose(g_srv, on_server_closed, &closed);
    WAIT(closed);
    uv_run(&g_loop, UV_RUN_DEFAULT);
    CHECK(g_live == 0);
    free(cl);
    free(g_runs);
    g_runs = NULL;
    g_maxIds = 0;
}

/* ---- Connection limit ----------------------------------------------------------- */

typedef struct ccall {
    int32_t id, zero, sum;
} ccall;

static int g_cOk, g_cLost, g_cOther;

static void on_conn_call(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    ccall *k = (ccall *)data;
    (void)c;
    (void)res;
    if (st == ONCRPC_OK && k->sum == k->id)
        g_cOk++;
    else if (st == ONCRPC_ERR_CANTRECV || st == ONCRPC_ERR_CANTSEND)
        g_cLost++;
    else
        g_cOther++;
}

static void on_ready(oncRPCClient *c, oncRPCStat st, void *data)
{
    (void)c;
    (void)st;
    (*(int *)data)++;
}

/* Connections the server has accepted or refused so far. */
static uint64_t connections_seen(void)
{
    oncRPCServerStats st;
    oncRPCServerGetStats(g_srv, &st);
    return st.connectionsAccepted + st.connectionsRefused;
}

static void conn_limit(void)
{
    enum { NC = 12, LIMIT = 8 };
    oncRPCClient *c[NC];
    ccall k[NC];
    oncRPCServerLimits lim;
    oncRPCServerStats st;
    int i, closed = 0, ready = 0;

    printf("  connection limit\n");
    g_cOk = g_cLost = g_cOther = 0;
    CHECK(oncRPCServerCreate(&g_loop, &A, &g_srv) == ONCRPC_OK);
    CHECK(oncRPCServerListen(g_srv, ONCRPC_PROTO_TCP, "127.0.0.1", 0) == ONCRPC_OK);
    CHECK(oncRPCCalcProgV1Register(g_srv, NULL) == ONCRPC_OK);
    oncRPCServerGetLimits(g_srv, &lim);
    lim.maxConnections = LIMIT;
    CHECK(oncRPCServerSetLimits(g_srv, &lim) == ONCRPC_OK);
    /* Connect one at a time, so that the first LIMIT are the ones kept. */
    for (i = 0; i < NC; i++) {
        CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1",
                                 oncRPCServerPort(g_srv, ONCRPC_PROTO_TCP), CALC_PROG, CALC_VERS,
                                 ONCRPC_PROTO_TCP, on_ready, &ready, &c[i]) == ONCRPC_OK);
        oncRPCClientSetTimeout(c[i], 5000);
        WAIT(ready == i + 1);
        WAIT(connections_seen() == (uint64_t)(i + 1));
    }
    for (i = 0; i < NC; i++) {
        k[i].id = i;
        k[i].zero = 0;
        k[i].sum = -1;
        CHECK(oncRPCCalcAddV1(c[i], &k[i].id, &k[i].zero, &k[i].sum, on_conn_call, &k[i]) ==
              ONCRPC_OK);
    }
    WAIT(g_cOk + g_cLost + g_cOther == NC);
    oncRPCServerGetStats(g_srv, &st);
    printf("    %d ok, %d calls lost; server refused %llu, has %lu open\n", g_cOk, g_cLost,
           (unsigned long long)st.connectionsRefused, (unsigned long)st.connections);
    CHECK(g_cOk == LIMIT && g_cLost == NC - LIMIT && g_cOther == 0);
    /* A refused client tries again when it calls, and is refused again. */
    CHECK(st.connectionsRefused >= (uint64_t)(NC - LIMIT) && st.connections == LIMIT);
    for (i = 0; i < NC; i++)
        oncRPCClientClose(c[i], NULL, NULL);
    oncRPCServerClose(g_srv, on_server_closed, &closed);
    WAIT(closed);
    uv_run(&g_loop, UV_RUN_DEFAULT);
    CHECK(g_live == 0);
}

int main(int argc, char **argv)
{
    phase ph;
    unsigned long seed = 1;
    int scale = 1;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1)
        seed = strtoul(argv[1], NULL, 10);
    if (argc > 2)
        scale = atoi(argv[2]);
    if (scale < 1)
        scale = 1;
    g_rng = (uint32_t)seed ? (uint32_t)seed : 1u;
    printf("  seed %lu, scale %d\n", seed, scale);
    CHECK(uv_loop_init(&g_loop) == 0);
#ifdef _WIN32
    snprintf(g_localPath, sizeof(g_localPath), "\\\\.\\pipe\\oncrpc-soak-%d",
             (int)uv_os_getpid());
#else
    {
        char dir[200];
        size_t dlen = sizeof(dir);
        if (uv_os_tmpdir(dir, &dlen) != 0)
            snprintf(dir, sizeof(dir), "/tmp");
        snprintf(g_localPath, sizeof(g_localPath), "%s/oncrpc-soak-%d.sock", dir,
                 (int)uv_os_getpid());
    }
#endif

    g_dropPct = 15;
    g_dupPct = 5;
    g_delayPct = 20;
    g_slowPct = 10;
    g_restPct = 2;

    /* 1. Cache big enough for every request and longer-lived than any
     *    client's patience: no UDP call may run twice. */
    memset(&ph, 0, sizeof(ph));
    ph.name = "lossy network, large cache: every call runs at most once";
    ph.lim.maxConnections = 0;
    ph.lim.maxRequests = 32;
    ph.lim.maxRequestsPerConnection = 4;
    ph.lim.idleTimeoutMs = 40;
    ph.lim.maxRecord = ONCRPC_DEFAULT_MAX_RECORD;
    ph.lim.duplicateCacheEntries = 1000000;
    ph.lim.duplicateCacheBytes = 0;
    ph.lim.duplicateCacheTtlMs = 60000;
    ph.udpClients = 40;
    ph.tcpClients = 20;
    ph.callsPerClient = 150 * scale;
    ph.strictOnce = 1;
    run_phase(&ph);

    /* 2. A tiny cache that keeps evicting and expiring: calls may run again,
     *    but every answer must still be right and the cache must stay within
     *    its limits. */
    ph.name = "lossy network, tiny cache: limits hold, answers stay right";
    ph.lim.duplicateCacheEntries = 16;
    ph.lim.duplicateCacheBytes = 300;
    ph.lim.duplicateCacheTtlMs = 100;
    ph.strictOnce = 0;
    run_phase(&ph);

    /* 3. More TCP clients than maxConnections: the extra connections are
     *    closed at once, their calls fail, and the others are unharmed. */
    conn_limit();

    CHECK(uv_loop_close(&g_loop) == 0);
    printf("test_soak: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
