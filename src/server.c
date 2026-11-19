/*
 * server.c - asynchronous RPC server on libuv serving any number of TCP and
 * UDP listeners. Requests may be answered inside the dispatch routine or
 * at any later time.
 */
#include "internal.h"

#include <stdio.h>
#include <string.h>

typedef struct slistener {
    union {
        uv_handle_t h;
        uv_tcp_t    tcp;
        uv_udp_t    udp;
    } io;
    oncRPCServer     *srv;
    oncRPCProto       proto;
    uint16_t          port;
    struct sockaddr_storage bound;  /* address actually bound */
    uint8_t          *ubuf;         /* UDP datagram buffer */
    struct slistener *next;
} slistener;

typedef struct sconn {
    uv_tcp_t                tcp;
    oncRPCServer           *srv;
    uint8_t                *buf;
    size_t                  len;
    size_t                  cap;
    struct sockaddr_storage peer;
    struct svcCall         *reqs;   /* outstanding requests from this connection */
    size_t                  nreqs;
    uint32_t                idleTicks;    /* housekeeping ticks with no traffic */
    int                     paused;       /* reading stopped by a request limit */
    int                     closing;
    struct sconn           *next;
    struct sconn           *prev;
} sconn;

/*
 * One remembered UDP request. While the request is being handled, req points
 * at it and a resend is ignored; once answered, the reply is kept and a
 * resend gets a copy. Entries live in a hash table (chained on hnext) and on
 * a list ordered by last use, newest first, for expiry and eviction.
 */
typedef struct drcEntry {
    struct drcEntry *hnext;
    struct drcEntry *newer;
    struct drcEntry *older;
    slistener       *lis;
    struct svcCall  *req;
    uint8_t         *reply;      /* reply datagram once answered */
    size_t           replylen;
    size_t           reqlen;
    uint64_t         stamp;      /* uv_now when answered */
    uint32_t         xid;
    uint32_t         bodyHash;   /* hash of the request after the xid */
    uint32_t         bucket;
    uint8_t          peerKey[20];
    uint8_t          peerKeyLen;
} drcEntry;

typedef struct sreg {
    uint32_t         prog;
    uint32_t         vers;
    oncRPCDispatchFn fn;
    void            *ctx;
    struct sreg     *next;
} sreg;

/* The full request; the public part comes first. */
typedef struct svcCall {
    oncRPCSvcReq            pub;
    oncRPCServer           *srv;
    sconn                  *conn;      /* TCP; NULL once the connection is gone */
    slistener              *lis;       /* UDP */
    struct sockaddr_storage peer;
    const uint8_t          *msg;       /* valid during dispatch only */
    size_t                  msglen;
    size_t                  argpos;
    void                   *args;
    oncRPCXdrProc           xargs;
    int                     inDispatch;
    int                     finished;
    oncRPCAuthSysParams     sys;
    drcEntry               *drc;       /* UDP duplicate cache entry, if any */
    struct svcCall         *next;      /* in conn->reqs */
    struct svcCall         *prev;
} svcCall;

/* A write/send request owning the reply bytes. */
typedef struct swreq {
    union {
        uv_write_t    w;
        uv_udp_send_t u;
    } r;
    oncRPCServer *srv;
    uint8_t      *buf;
} swreq;

struct oncRPCServer {
    uv_loop_t             *loop;
    const oncRPCAllocator *alloc;
    slistener             *listeners;
    sconn                 *conns;
    sreg                  *regs;
    oncRPCServerLimits     lim;
    oncRPCServerStats      stats;
    size_t                 pendingReqs;   /* dispatched, not finished */
    size_t                 activeCloses;  /* uv_close callbacks outstanding */
    size_t                 activeOps;     /* writes/sends and port mapper ops */
    size_t                 nconns;
    size_t                 pausedConns;
    uint8_t               *msg;           /* reassembled TCP record */
    size_t                 msgcap;
    uv_timer_t             tick;          /* idle connections, cache expiry */
    uint64_t               tickMs;        /* its period; 0 when stopped */
    uv_idle_t              resume;        /* restarts paused connections */
    drcEntry             **drcTable;
    uint32_t               drcBuckets;    /* power of two */
    drcEntry              *drcNewest;
    drcEntry              *drcOldest;
    size_t                 drcEntries;
    size_t                 drcBytes;
    int                    closing;
    oncRPCCloseCb          closeCb;
    void                  *closeData;
};

#define ONCRPC_REQ(r) ((svcCall *)(void *)(r))

static void conn_close(sconn *c);
static void on_resume(uv_idle_t *h);
static void on_handle_closed(uv_handle_t *h);
static void udp_send_copy(slistener *l, const struct sockaddr *to, const uint8_t *data,
                          size_t len);

/* ---- UDP duplicate-request cache ------------------------------------------------ */

static uint32_t fnv1a(uint32_t h, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* The bytes that identify a peer: family, port and address. */
static uint8_t peer_key(const struct sockaddr *a, uint8_t key[20])
{
    if (a->sa_family == AF_INET6) {
        const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *)(const void *)a;
        key[0] = 6;
        memcpy(key + 1, &a6->sin6_port, 2);
        memcpy(key + 3, &a6->sin6_addr, 16);
        return 19;
    } else {
        const struct sockaddr_in *a4 = (const struct sockaddr_in *)(const void *)a;
        key[0] = 4;
        memcpy(key + 1, &a4->sin_port, 2);
        memcpy(key + 3, &a4->sin_addr, 4);
        return 7;
    }
}

static int drc_enabled(const oncRPCServer *s)
{
    return s->lim.duplicateCacheEntries > 0 && !s->closing;
}

static void drc_unlink_age(oncRPCServer *s, drcEntry *e)
{
    if (e->newer)
        e->newer->older = e->older;
    else
        s->drcNewest = e->older;
    if (e->older)
        e->older->newer = e->newer;
    else
        s->drcOldest = e->newer;
    e->newer = e->older = NULL;
}

static void drc_push_newest(oncRPCServer *s, drcEntry *e)
{
    e->older = s->drcNewest;
    e->newer = NULL;
    if (s->drcNewest)
        s->drcNewest->newer = e;
    else
        s->drcOldest = e;
    s->drcNewest = e;
}

static void drc_remove(oncRPCServer *s, drcEntry *e)
{
    drcEntry **pp = &s->drcTable[e->bucket];
    while (*pp != e)
        pp = &(*pp)->hnext;
    *pp = e->hnext;
    drc_unlink_age(s, e);
    if (e->req != NULL)
        e->req->drc = NULL;
    s->drcEntries--;
    s->drcBytes -= e->replylen;
    oncRPCFree(s->alloc, e->reply);
    oncRPCFree(s->alloc, e);
}

static void drc_clear(oncRPCServer *s)
{
    while (s->drcOldest != NULL)
        drc_remove(s, s->drcOldest);
    oncRPCFree(s->alloc, s->drcTable);
    s->drcTable = NULL;
    s->drcBuckets = 0;
}

static int drc_expired(const oncRPCServer *s, const drcEntry *e, uint64_t now)
{
    return e->req == NULL && s->lim.duplicateCacheTtlMs > 0 &&
           now - e->stamp >= s->lim.duplicateCacheTtlMs;
}

/* Drop answered entries that have expired. Answered entries are ordered by
 * the time of their reply, so the walk stops at the first live one. */
static void drc_expire(oncRPCServer *s, uint64_t now)
{
    drcEntry *e = s->drcOldest;
    while (e != NULL) {
        drcEntry *newer = e->newer;
        if (e->req == NULL) {
            if (!drc_expired(s, e, now))
                break;
            drc_remove(s, e);
        }
        e = newer;
    }
}

/* Evict the oldest answered entries until one more entry with extra reply
 * bytes fits. Entries still being handled are never evicted. Returns 0 if
 * there is no room. */
static int drc_make_room(oncRPCServer *s, size_t entries, size_t bytes, const drcEntry *keep)
{
    drcEntry *e = s->drcOldest;
    const size_t maxBytes = s->lim.duplicateCacheBytes;

    if (maxBytes > 0 && bytes > maxBytes)
        return 0;
    while (s->drcEntries + entries > s->lim.duplicateCacheEntries ||
           (maxBytes > 0 && s->drcBytes + bytes > maxBytes)) {
        while (e != NULL && (e->req != NULL || e == keep))
            e = e->newer;
        if (e == NULL)
            return 0;
        {
            drcEntry *newer = e->newer;
            drc_remove(s, e);
            e = newer;
        }
    }
    return 1;
}

static uint32_t drc_bucket(const oncRPCServer *s, uint32_t xid, const uint8_t *key, size_t keylen)
{
    uint8_t x[4];
    oncRPCIntPutBe32(x, xid);
    return fnv1a(fnv1a(2166136261u, x, 4), key, keylen) & (s->drcBuckets - 1);
}

static drcEntry *drc_find(oncRPCServer *s, const slistener *l, uint32_t xid, uint32_t bodyHash,
                          size_t reqlen, const uint8_t *key, uint8_t keylen)
{
    drcEntry *e;
    if (s->drcTable == NULL)
        return NULL;
    for (e = s->drcTable[drc_bucket(s, xid, key, keylen)]; e != NULL; e = e->hnext)
        if (e->xid == xid && e->lis == l && e->bodyHash == bodyHash && e->reqlen == reqlen &&
            e->peerKeyLen == keylen && memcmp(e->peerKey, key, keylen) == 0)
            return e;
    return NULL;
}

static drcEntry *drc_insert(oncRPCServer *s, slistener *l, uint32_t xid, uint32_t bodyHash,
                            size_t reqlen, const uint8_t *key, uint8_t keylen)
{
    drcEntry *e;

    if (s->drcTable == NULL) {
        uint32_t n = 16;
        while (n < s->lim.duplicateCacheEntries && n < 65536u)
            n <<= 1;
        s->drcTable = (drcEntry **)oncRPCAllocArray(s->alloc, n, sizeof(drcEntry *));
        if (s->drcTable == NULL)
            return NULL;
        s->drcBuckets = n;
    }
    if (!drc_make_room(s, 1, 0, NULL))
        return NULL;
    e = (drcEntry *)oncRPCAlloc(s->alloc, sizeof(*e));
    if (e == NULL)
        return NULL;
    e->lis = l;
    e->xid = xid;
    e->bodyHash = bodyHash;
    e->reqlen = reqlen;
    memcpy(e->peerKey, key, keylen);
    e->peerKeyLen = keylen;
    e->bucket = drc_bucket(s, xid, key, keylen);
    e->hnext = s->drcTable[e->bucket];
    s->drcTable[e->bucket] = e;
    drc_push_newest(s, e);
    s->drcEntries++;
    return e;
}

/* The request behind e has been answered with this datagram: keep a copy. */
static void drc_answered(oncRPCServer *s, drcEntry *e, const uint8_t *reply, size_t len)
{
    e->req->drc = NULL;
    e->req = NULL;
    if (!drc_make_room(s, 0, len, e) ||
        (e->reply = (uint8_t *)oncRPCRealloc(s->alloc, NULL, len)) == NULL) {
        drc_remove(s, e);
        return;
    }
    memcpy(e->reply, reply, len);
    e->replylen = len;
    s->drcBytes += len;
    e->stamp = uv_now(s->loop);
    drc_unlink_age(s, e);
    drc_push_newest(s, e);
}

/*
 * Look a UDP datagram up in the cache. Returns 1 if it was a resend that has
 * been dealt with; otherwise 0, with *entry set to a new in-progress entry
 * for it (or NULL if it is not cached).
 */
static int drc_check(slistener *l, const uint8_t *msg, size_t len, const struct sockaddr *from,
                     drcEntry **entry)
{
    oncRPCServer *s = l->srv;
    uint8_t key[20];
    uint8_t keylen;
    uint32_t xid, bodyHash;
    drcEntry *e;

    *entry = NULL;
    if (!drc_enabled(s) || len < 8 || oncRPCIntGetBe32(msg + 4) != ONCRPC_MSG_CALL)
        return 0;
    xid = oncRPCIntGetBe32(msg);
    bodyHash = fnv1a(2166136261u, msg + 4, len - 4);
    keylen = peer_key(from, key);
    e = drc_find(s, l, xid, bodyHash, len, key, keylen);
    if (e != NULL) {
        if (e->req != NULL) {
            s->stats.duplicatesDropped++;
            return 1;
        }
        if (!drc_expired(s, e, uv_now(s->loop))) {
            s->stats.duplicatesReplayed++;
            udp_send_copy(l, from, e->reply, e->replylen);
            return 1;
        }
        drc_remove(s, e);
    }
    *entry = drc_insert(s, l, xid, bodyHash, len, key, keylen);
    return 0;
}

/* ---- Connection pausing -------------------------------------------------------- */

static int conn_may_dispatch(const sconn *c)
{
    const oncRPCServer *s = c->srv;
    return (s->lim.maxRequestsPerConnection == 0 ||
            c->nreqs < s->lim.maxRequestsPerConnection) &&
           (s->lim.maxRequests == 0 || s->pendingReqs < s->lim.maxRequests);
}

static void conn_pause(sconn *c)
{
    oncRPCServer *s = c->srv;
    if (c->paused)
        return;
    c->paused = 1;
    s->pausedConns++;
    s->stats.connectionsPaused++;
    (void)uv_read_stop((uv_stream_t *)&c->tcp);
}

/* Paused connections are restarted from an idle callback rather than from
 * inside oncRPCSvcReply and friends, so that a reply never dispatches new
 * requests from within the caller. */
static void schedule_resume(oncRPCServer *s)
{
    if (s->pausedConns > 0 && !s->closing)
        (void)uv_idle_start(&s->resume, on_resume);
}

/* ---- Close bookkeeping ------------------------------------------------------- */

static void maybe_finish_close(oncRPCServer *s)
{
    oncRPCCloseCb cb;
    void *data;

    if (!s->closing || s->activeCloses > 0 || s->pendingReqs > 0 || s->activeOps > 0)
        return;
    while (s->regs != NULL) {
        sreg *r = s->regs;
        s->regs = r->next;
        oncRPCFree(s->alloc, r);
    }
    cb = s->closeCb;
    data = s->closeData;
    oncRPCFree(s->alloc, s->msg);
    oncRPCFree(s->alloc, s);
    if (cb)
        cb(data);
}

/* ---- Request lifecycle -------------------------------------------------------- */

static void release(svcCall *r)
{
    oncRPCServer *s = r->srv;

    if (r->args != NULL) {
        oncRPCXdrFree(s->alloc, r->xargs, r->args);
        oncRPCFree(s->alloc, r->args);
    }
    if (r->drc != NULL)   /* finished without a reply: forget the request */
        drc_remove(s, r->drc);
    if (r->conn != NULL) {
        sconn *c = r->conn;
        if (r->prev)
            r->prev->next = r->next;
        else
            c->reqs = r->next;
        if (r->next)
            r->next->prev = r->prev;
        c->nreqs--;
        c->idleTicks = 0;
    }
    oncRPCFree(s->alloc, r);
    s->pendingReqs--;
    schedule_resume(s);
    maybe_finish_close(s);
}

static void finish(svcCall *r)
{
    if (r->finished)
        return;
    r->finished = 1;
    if (!r->inDispatch)
        release(r);
}

/* ---- Sending replies ------------------------------------------------------------ */

static void on_written(uv_write_t *req, int status)
{
    swreq *w = (swreq *)req;
    oncRPCServer *s = w->srv;
    (void)status;   /* a failed write shows up as a read error */
    oncRPCFree(s->alloc, w->buf);
    oncRPCFree(s->alloc, w);
    s->activeOps--;
    maybe_finish_close(s);
}

static void on_sent(uv_udp_send_t *req, int status)
{
    on_written((uv_write_t *)(void *)req, status);
}

/* Transmit an encoded reply (4 bytes reserved for the record mark). */
static void transmit(svcCall *r, uint8_t *buf, size_t len)
{
    oncRPCServer *s = r->srv;
    swreq *w;
    uv_buf_t b;
    int rc;

    if (s->closing || (r->pub.proto == ONCRPC_PROTO_TCP && (r->conn == NULL || r->conn->closing)) ||
        (r->pub.proto == ONCRPC_PROTO_UDP && r->lis == NULL) ||
        (w = (swreq *)oncRPCAlloc(s->alloc, sizeof(*w))) == NULL) {
        oncRPCFree(s->alloc, buf);
        return;
    }
    w->srv = s;
    w->buf = buf;
    if (r->drc != NULL)
        drc_answered(s, r->drc, buf + 4, len - 4);
    if (r->pub.proto == ONCRPC_PROTO_TCP) {
        oncRPCIntPutBe32(buf, (uint32_t)(len - 4) | ONCRPC_LAST_FRAGMENT);
        b = uv_buf_init((char *)buf, (unsigned int)len);
        rc = uv_write(&w->r.w, (uv_stream_t *)&r->conn->tcp, &b, 1, on_written);
    } else {
        b = uv_buf_init((char *)buf + 4, (unsigned int)(len - 4));
        rc = uv_udp_send(&w->r.u, &r->lis->io.udp, &b, 1,
                         (const struct sockaddr *)&r->peer, on_sent);
    }
    if (rc != 0) {
        oncRPCFree(s->alloc, buf);
        oncRPCFree(s->alloc, w);
        return;
    }
    s->activeOps++;
}

/* Send a copy of a datagram (a cached reply). */
static void udp_send_copy(slistener *l, const struct sockaddr *to, const uint8_t *data,
                          size_t len)
{
    oncRPCServer *s = l->srv;
    swreq *w = (swreq *)oncRPCAlloc(s->alloc, sizeof(*w));
    uv_buf_t b;

    if (w == NULL)
        return;
    w->srv = s;
    w->buf = (uint8_t *)oncRPCRealloc(s->alloc, NULL, len);
    if (w->buf == NULL) {
        oncRPCFree(s->alloc, w);
        return;
    }
    memcpy(w->buf, data, len);
    b = uv_buf_init((char *)w->buf, (unsigned int)len);
    if (uv_udp_send(&w->r.u, &l->io.udp, &b, 1, to, on_sent) != 0) {
        oncRPCFree(s->alloc, w->buf);
        oncRPCFree(s->alloc, w);
        return;
    }
    s->activeOps++;
}

/* A port in network byte order, as found in a sockaddr. Done by hand so
 * that the library needs no socket library of its own (ntohs is in
 * ws2_32 on Windows). */
static unsigned port_of(const void *net_order_port)
{
    const uint8_t *p = (const uint8_t *)net_order_port;
    return ((unsigned)p[0] << 8) | p[1];
}

static int put_u32(oncRPCXdr *x, uint32_t v)
{
    return oncRPCXdrUint32(x, &v);
}

static int begin_reply(svcCall *r, oncRPCXdr *x, uint32_t reply_stat)
{
    if (!oncRPCXdrInitEncodeDynamic(x, r->srv->alloc, 256))
        return 0;
    return put_u32(x, 0) && put_u32(x, r->pub.xid) && put_u32(x, ONCRPC_MSG_REPLY) &&
           put_u32(x, reply_stat);
}

static int encode_accepted(svcCall *r, oncRPCXdr *x, uint32_t stat, uint32_t low,
                           uint32_t high)
{
    if (!begin_reply(r, x, ONCRPC_MSG_ACCEPTED) || !put_u32(x, ONCRPC_AUTH_NONE) ||
        !put_u32(x, 0) || !put_u32(x, stat))
        return 0;
    if (stat == ONCRPC_ACCEPT_PROG_MISMATCH)
        return put_u32(x, low) && put_u32(x, high);
    return 1;
}

static void send_and_finish(svcCall *r, oncRPCXdr *x)
{
    size_t len;
    uint8_t *buf = oncRPCXdrDetach(x, &len);
    if (buf != NULL)
        transmit(r, buf, len);
    finish(r);
}

static void reply_accepted(svcCall *r, uint32_t stat, uint32_t low, uint32_t high)
{
    oncRPCXdr x;
    if (r->finished)
        return;
    if (encode_accepted(r, &x, stat, low, high))
        send_and_finish(r, &x);
    else {
        oncRPCXdrDestroy(&x);
        finish(r);
    }
}

static void reply_denied(svcCall *r, uint32_t rej, uint32_t a, uint32_t b)
{
    oncRPCXdr x;
    int ok;
    if (r->finished)
        return;
    ok = begin_reply(r, &x, ONCRPC_MSG_DENIED) && put_u32(&x, rej) && put_u32(&x, a) &&
         (rej != ONCRPC_REJ_RPC_MISMATCH || put_u32(&x, b));
    if (ok)
        send_and_finish(r, &x);
    else {
        oncRPCXdrDestroy(&x);
        finish(r);
    }
}

int oncRPCSvcReply(oncRPCSvcReq *req, oncRPCXdrProc xres, const void *res)
{
    svcCall *r = ONCRPC_REQ(req);
    oncRPCXdr x;

    if (r == NULL || r->finished)
        return 0;
    if (encode_accepted(r, &x, ONCRPC_ACCEPT_SUCCESS, 0, 0) &&
        (xres == NULL || xres(&x, (void *)res))) {
        send_and_finish(r, &x);
        return 1;
    }
    oncRPCXdrDestroy(&x);
    reply_accepted(r, ONCRPC_ACCEPT_SYSTEM_ERR, 0, 0);
    return 0;
}

void oncRPCSvcDrop(oncRPCSvcReq *req)
{
    if (req != NULL)
        finish(ONCRPC_REQ(req));
}

void oncRPCSvcErrNoProc(oncRPCSvcReq *req)
{
    if (req != NULL)
        reply_accepted(ONCRPC_REQ(req), ONCRPC_ACCEPT_PROC_UNAVAIL, 0, 0);
}

void oncRPCSvcErrDecode(oncRPCSvcReq *req)
{
    if (req != NULL)
        reply_accepted(ONCRPC_REQ(req), ONCRPC_ACCEPT_GARBAGE_ARGS, 0, 0);
}

void oncRPCSvcErrSystem(oncRPCSvcReq *req)
{
    if (req != NULL)
        reply_accepted(ONCRPC_REQ(req), ONCRPC_ACCEPT_SYSTEM_ERR, 0, 0);
}

void oncRPCSvcErrNoProg(oncRPCSvcReq *req)
{
    if (req != NULL)
        reply_accepted(ONCRPC_REQ(req), ONCRPC_ACCEPT_PROG_UNAVAIL, 0, 0);
}

void oncRPCSvcErrProgVers(oncRPCSvcReq *req, uint32_t low, uint32_t high)
{
    if (req != NULL)
        reply_accepted(ONCRPC_REQ(req), ONCRPC_ACCEPT_PROG_MISMATCH, low, high);
}

void oncRPCSvcErrAuth(oncRPCSvcReq *req, oncRPCAuthStat why)
{
    if (req != NULL)
        reply_denied(ONCRPC_REQ(req), ONCRPC_REJ_AUTH_ERROR, (uint32_t)why, 0);
}

void *oncRPCSvcDecodeArgs(oncRPCSvcReq *req, oncRPCXdrProc xargs, size_t size)
{
    svcCall *r = ONCRPC_REQ(req);
    oncRPCXdr x;
    void *a;

    if (r == NULL || r->finished || !r->inDispatch || r->msg == NULL || r->args != NULL ||
        xargs == NULL)
        return NULL;
    a = oncRPCAlloc(r->srv->alloc, size);
    if (a == NULL)
        return NULL;
    oncRPCXdrInitDecode(&x, r->srv->alloc, r->msg, r->msglen);
    if (!oncRPCXdrSetPos(&x, r->argpos) || !xargs(&x, a)) {
        oncRPCXdrFree(r->srv->alloc, xargs, a);
        oncRPCFree(r->srv->alloc, a);
        return NULL;
    }
    r->args = a;
    r->xargs = xargs;
    return a;
}

int oncRPCSvcPeer(const oncRPCSvcReq *req, char *buf, size_t len)
{
    const svcCall *r = (const svcCall *)(const void *)req;
    char host[64];

    if (r == NULL || buf == NULL || len == 0)
        return -1;
    if (r->peer.ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)&r->peer;
        if (uv_ip4_name(a, host, sizeof(host)) != 0)
            return -1;
        snprintf(buf, len, "%s:%u", host, port_of(&a->sin_port));
        return 0;
    }
    if (r->peer.ss_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)&r->peer;
        if (uv_ip6_name(a, host, sizeof(host)) != 0)
            return -1;
        snprintf(buf, len, "[%s]:%u", host, port_of(&a->sin6_port));
        return 0;
    }
    return -1;
}

/* ---- Incoming calls -------------------------------------------------------------- */

/* Returns 1 if a request was created (it may already be finished). */
static int handle_call(oncRPCServer *s, const uint8_t *msg, size_t len, oncRPCProto proto,
                       sconn *conn, slistener *lis, const struct sockaddr_storage *peer,
                       drcEntry *drc)
{
    svcCall *r;
    oncRPCXdr x;
    oncRPCOpaqueAuth cred, verf;
    uint32_t mtype, rpcvers, low = UINT32_MAX, high = 0;
    sreg *g, *match = NULL;
    int found_prog = 0;

    if (s->closing)
        return 0;
    oncRPCXdrInitDecode(&x, s->alloc, msg, len);
    {
        uint32_t xid;
        if (!oncRPCXdrUint32(&x, &xid) || !oncRPCXdrUint32(&x, &mtype) ||
            mtype != ONCRPC_MSG_CALL || !oncRPCXdrUint32(&x, &rpcvers))
            return 0;   /* not a call: ignore silently */
        r = (svcCall *)oncRPCAlloc(s->alloc, sizeof(*r));
        if (r == NULL)
            return 0;   /* no memory: the client will retry or time out */
        r->pub.xid = xid;
    }
    r->srv = s;
    r->pub.server = s;
    r->pub.proto = proto;
    r->pub.priv = (oncRPCSvcPriv *)(void *)r;
    r->lis = lis;
    memcpy(&r->peer, peer, sizeof(r->peer));
    if (conn != NULL) {
        r->conn = conn;
        r->next = conn->reqs;
        if (conn->reqs)
            conn->reqs->prev = r;
        conn->reqs = r;
        conn->nreqs++;
    }
    if (drc != NULL) {
        r->drc = drc;
        drc->req = r;
    }
    s->pendingReqs++;
    s->stats.requests++;

    if (rpcvers != ONCRPC_PROTOCOL_VERSION) {
        reply_denied(r, ONCRPC_REJ_RPC_MISMATCH, ONCRPC_PROTOCOL_VERSION,
                     ONCRPC_PROTOCOL_VERSION);
        return 1;
    }
    if (!oncRPCXdrUint32(&x, &r->pub.prog) || !oncRPCXdrUint32(&x, &r->pub.vers) ||
        !oncRPCXdrUint32(&x, &r->pub.proc) || !oncRPCIntXdrOpaqueAuth(&x, &cred) ||
        !oncRPCIntXdrOpaqueAuth(&x, &verf)) {
        reply_accepted(r, ONCRPC_ACCEPT_GARBAGE_ARGS, 0, 0);
        return 1;
    }
    r->pub.credFlavor = cred.flavor;
    if (cred.flavor == ONCRPC_AUTH_SYS) {
        oncRPCXdr ax;
        oncRPCXdrInitDecode(&ax, NULL, cred.body, cred.len);
        if (!oncRPCXdrAuthSys(&ax, &r->sys)) {
            reply_denied(r, ONCRPC_REJ_AUTH_ERROR, ONCRPC_AUTH_BADCRED, 0);
            return 1;
        }
        r->pub.authSys = &r->sys;
    } else if (cred.flavor != ONCRPC_AUTH_NONE) {
        reply_denied(r, ONCRPC_REJ_AUTH_ERROR, ONCRPC_AUTH_REJECTEDCRED, 0);
        return 1;
    }

    for (g = s->regs; g != NULL; g = g->next) {
        if (g->prog != r->pub.prog)
            continue;
        found_prog = 1;
        if (g->vers < low)
            low = g->vers;
        if (g->vers > high)
            high = g->vers;
        if (g->vers == r->pub.vers)
            match = g;
    }
    if (match == NULL) {
        if (found_prog)
            reply_accepted(r, ONCRPC_ACCEPT_PROG_MISMATCH, low, high);
        else
            reply_accepted(r, ONCRPC_ACCEPT_PROG_UNAVAIL, 0, 0);
        return 1;
    }

    r->pub.ctx = match->ctx;
    r->msg = msg;
    r->msglen = len;
    r->argpos = oncRPCXdrGetPos(&x);
    r->inDispatch = 1;
    match->fn(&r->pub);
    r->inDispatch = 0;
    r->msg = NULL;
    if (r->finished)
        release(r);
    return 1;
}

static void on_conn_closed(uv_handle_t *h)
{
    sconn *c = (sconn *)h->data;
    oncRPCServer *s = c->srv;
    oncRPCFree(s->alloc, c->buf);
    oncRPCFree(s->alloc, c);
    s->activeCloses--;
    maybe_finish_close(s);
}

static void conn_close(sconn *c)
{
    oncRPCServer *s = c->srv;
    svcCall *r;

    if (c->closing)
        return;
    c->closing = 1;
    if (c->paused) {
        c->paused = 0;
        s->pausedConns--;
    }
    s->nconns--;
    /* Outstanding requests survive; their replies will be discarded. */
    for (r = c->reqs; r != NULL; r = r->next)
        r->conn = NULL;
    c->reqs = NULL;
    if (c->prev)
        c->prev->next = c->next;
    else
        s->conns = c->next;
    if (c->next)
        c->next->prev = c->prev;
    s->activeCloses++;
    uv_close((uv_handle_t *)&c->tcp, on_conn_closed);
}

static void on_conn_alloc(uv_handle_t *h, size_t suggested, uv_buf_t *b)
{
    sconn *c = (sconn *)h->data;
    oncRPCServer *s = c->srv;
    (void)suggested;
    if (c->cap - c->len < ONCRPC_READ_CHUNK &&
        (c->len > s->lim.maxRecord + ONCRPC_READ_CHUNK ||
         !oncRPCIntGrow(s->alloc, &c->buf, &c->cap, c->len + ONCRPC_READ_CHUNK))) {
        *b = uv_buf_init(NULL, 0);   /* reported as UV_ENOBUFS: connection dropped */
        return;
    }
    *b = uv_buf_init((char *)c->buf + c->len, (unsigned int)(c->cap - c->len));
}

/* Dispatch the complete records buffered for a connection, pausing it when a
 * request limit is reached. */
static void conn_process(sconn *c)
{
    oncRPCServer *s = c->srv;
    size_t mlen;
    int r;

    while (!c->closing && !s->closing) {
        if (!conn_may_dispatch(c)) {
            conn_pause(c);
            return;
        }
        r = oncRPCIntRecordExtract(s->alloc, c->buf, &c->len, s->lim.maxRecord, &s->msg,
                                   &s->msgcap, &mlen);
        if (r == 0)
            return;
        if (r < 0) {
            conn_close(c);
            return;
        }
        (void)handle_call(s, s->msg, mlen, ONCRPC_PROTO_TCP, c, NULL, &c->peer, NULL);
    }
}

static void on_conn_read(uv_stream_t *st, ssize_t nread, const uv_buf_t *b)
{
    sconn *c = (sconn *)st->data;
    (void)b;

    if (nread < 0) {
        conn_close(c);
        return;
    }
    if (nread == 0)
        return;
    c->len += (size_t)nread;
    c->idleTicks = 0;
    conn_process(c);
}

static void on_resume(uv_idle_t *h)
{
    oncRPCServer *s = (oncRPCServer *)h->data;
    sconn *c, *next;

    (void)uv_idle_stop(h);
    for (c = s->conns; c != NULL && !s->closing && s->pausedConns > 0; c = next) {
        next = c->next;
        if (!c->paused || !conn_may_dispatch(c))
            continue;
        c->paused = 0;
        s->pausedConns--;
        conn_process(c);
        if (!c->closing && !c->paused &&
            uv_read_start((uv_stream_t *)&c->tcp, on_conn_alloc, on_conn_read) != 0)
            conn_close(c);
    }
}

static void on_connection(uv_stream_t *server, int status)
{
    slistener *l = (slistener *)server->data;
    oncRPCServer *s = l->srv;
    sconn *c;
    int len = (int)sizeof(struct sockaddr_storage);

    if (status < 0 || s->closing)
        return;
    c = (sconn *)oncRPCAlloc(s->alloc, sizeof(*c));
    if (c == NULL || uv_tcp_init(s->loop, &c->tcp) != 0) {
        /* Cannot accept without a handle; the peer will see a reset/timeout. */
        oncRPCFree(s->alloc, c);
        return;
    }
    c->srv = s;
    c->tcp.data = c;
    c->idleTicks = 0;
    c->next = s->conns;
    if (s->conns)
        s->conns->prev = c;
    s->conns = c;
    s->nconns++;
    if (uv_accept(server, (uv_stream_t *)&c->tcp) != 0) {
        conn_close(c);
        return;
    }
    if (s->lim.maxConnections > 0 && s->nconns > s->lim.maxConnections) {
        s->stats.connectionsRefused++;
        conn_close(c);
        return;
    }
    s->stats.connectionsAccepted++;
    (void)uv_tcp_nodelay(&c->tcp, 1);
    (void)uv_tcp_getpeername(&c->tcp, (struct sockaddr *)&c->peer, &len);
    if (uv_read_start((uv_stream_t *)&c->tcp, on_conn_alloc, on_conn_read) != 0)
        conn_close(c);
}

static void on_udp_alloc(uv_handle_t *h, size_t suggested, uv_buf_t *b)
{
    slistener *l = (slistener *)h->data;
    (void)suggested;
    *b = uv_buf_init((char *)l->ubuf, ONCRPC_UDP_MAX);
}

static void on_udp_recv(uv_udp_t *u, ssize_t nread, const uv_buf_t *b,
                        const struct sockaddr *addr, unsigned flags)
{
    slistener *l = (slistener *)u->data;
    oncRPCServer *s = l->srv;
    const uint8_t *msg = (const uint8_t *)b->base;
    struct sockaddr_storage peer;
    drcEntry *drc;

    if (nread <= 0 || addr == NULL || (flags & UV_UDP_PARTIAL) || s->closing)
        return;
    if (drc_check(l, msg, (size_t)nread, addr, &drc))
        return;   /* a resend, answered from the cache or still in hand */
    if (s->lim.maxRequests > 0 && s->pendingReqs >= s->lim.maxRequests) {
        s->stats.requestsDroppedBusy++;
        if (drc != NULL)
            drc_remove(s, drc);
        return;
    }
    memset(&peer, 0, sizeof(peer));
    memcpy(&peer, addr, addr->sa_family == AF_INET6 ? sizeof(struct sockaddr_in6)
                                                    : sizeof(struct sockaddr_in));
    if (!handle_call(s, msg, (size_t)nread, ONCRPC_PROTO_UDP, NULL, l, &peer, drc) &&
        drc != NULL)
        drc_remove(s, drc);
}

/*
 * Periodic housekeeping: close idle connections and expire cached replies.
 *
 * Idleness is counted in ticks rather than measured against the loop's
 * clock. libuv runs timers before it reads sockets, so when the loop is busy
 * enough that one iteration outlasts the idle timeout, a connection whose
 * next request is already waiting unread would look idle by the clock. By
 * ticks, that connection is reset by the read in the same iteration and is
 * closed only after the timeout has passed over at least two quiet ticks.
 */
static void on_tick(uv_timer_t *t)
{
    oncRPCServer *s = (oncRPCServer *)t->data;
    sconn *c, *next;

    if (s->closing)
        return;
    if (s->lim.idleTimeoutMs > 0 && s->tickMs > 0) {
        for (c = s->conns; c != NULL; c = next) {
            next = c->next;
            if (c->nreqs > 0 || c->paused) {
                c->idleTicks = 0;
                continue;
            }
            c->idleTicks++;
            if (c->idleTicks >= 2 && (uint64_t)c->idleTicks * s->tickMs >= s->lim.idleTimeoutMs) {
                s->stats.connectionsIdleClosed++;
                conn_close(c);
            }
        }
    }
    drc_expire(s, uv_now(s->loop));
}

/* Run the housekeeping timer often enough for the shortest time limit. */
static void restart_tick(oncRPCServer *s)
{
    uint64_t period = 0;
    uint32_t t[2];
    int i;

    t[0] = s->lim.idleTimeoutMs;
    t[1] = s->lim.duplicateCacheEntries > 0 ? s->lim.duplicateCacheTtlMs : 0;
    for (i = 0; i < 2; i++)
        if (t[i] > 0 && (period == 0 || t[i] / 4 < period))
            period = t[i] / 4;
    (void)uv_timer_stop(&s->tick);
    s->tickMs = 0;
    if (period == 0 || s->closing)
        return;
    if (period < 10)
        period = 10;
    if (period > 1000)
        period = 1000;
    if (uv_timer_start(&s->tick, on_tick, period, period) == 0)
        s->tickMs = period;
}

/* ---- Server object ------------------------------------------------------------------- */

oncRPCStat oncRPCServerCreate(uv_loop_t *loop, const oncRPCAllocator *alloc, oncRPCServer **out)
{
    oncRPCServer *s;
    if (out != NULL)
        *out = NULL;
    if (loop == NULL || alloc == NULL || alloc->realloc == NULL || alloc->free == NULL ||
        out == NULL)
        return ONCRPC_ERR_INVALID;
    s = (oncRPCServer *)oncRPCAlloc(alloc, sizeof(*s));
    if (s == NULL)
        return ONCRPC_ERR_NOMEM;
    s->loop = loop;
    s->alloc = alloc;
    s->lim.maxConnections = ONCRPC_DEFAULT_MAX_CONNECTIONS;
    s->lim.maxRequests = ONCRPC_DEFAULT_MAX_REQUESTS;
    s->lim.maxRequestsPerConnection = ONCRPC_DEFAULT_MAX_REQUESTS_PER_CONNECTION;
    s->lim.idleTimeoutMs = ONCRPC_DEFAULT_IDLE_TIMEOUT_MS;
    s->lim.maxRecord = ONCRPC_DEFAULT_MAX_RECORD;
    s->lim.duplicateCacheEntries = ONCRPC_DEFAULT_DRC_ENTRIES;
    s->lim.duplicateCacheBytes = ONCRPC_DEFAULT_DRC_BYTES;
    s->lim.duplicateCacheTtlMs = ONCRPC_DEFAULT_DRC_TTL_MS;
    if (uv_timer_init(loop, &s->tick) != 0) {
        oncRPCFree(alloc, s);
        return ONCRPC_ERR_FAILED;
    }
    if (uv_idle_init(loop, &s->resume) != 0) {
        /* The timer must still be closed before the memory goes. */
        s->closing = 1;
        s->tick.data = s;
        s->activeCloses = 1;
        uv_close((uv_handle_t *)&s->tick, on_handle_closed);
        return ONCRPC_ERR_FAILED;
    }
    s->tick.data = s;
    s->resume.data = s;
    /* Housekeeping alone must not keep the loop running. */
    uv_unref((uv_handle_t *)&s->tick);
    restart_tick(s);
    *out = s;
    return ONCRPC_OK;
}

static void on_handle_closed(uv_handle_t *h)
{
    oncRPCServer *s = (oncRPCServer *)h->data;
    s->activeCloses--;
    maybe_finish_close(s);
}

static void on_listener_closed(uv_handle_t *h)
{
    slistener *l = (slistener *)h->data;
    oncRPCServer *s = l->srv;
    oncRPCFree(s->alloc, l->ubuf);
    oncRPCFree(s->alloc, l);
    s->activeCloses--;
    maybe_finish_close(s);
}

void oncRPCServerClose(oncRPCServer *s, oncRPCCloseCb cb, void *data)
{
    if (s == NULL || s->closing)
        return;
    s->closing = 1;
    s->closeCb = cb;
    s->closeData = data;
    drc_clear(s);
    s->activeCloses += 2;
    uv_close((uv_handle_t *)&s->tick, on_handle_closed);
    uv_close((uv_handle_t *)&s->resume, on_handle_closed);
    while (s->listeners != NULL) {
        slistener *l = s->listeners;
        s->listeners = l->next;
        s->activeCloses++;
        uv_close(&l->io.h, on_listener_closed);
    }
    while (s->conns != NULL)
        conn_close(s->conns);
    /* Requests still outstanding now hold no listener; replies are dropped. */
    maybe_finish_close(s);
}

oncRPCStat oncRPCServerListen(oncRPCServer *s, oncRPCProto proto, const char *ip, uint16_t port)
{
    struct sockaddr_storage addr;
    slistener *l;
    int r, namelen = (int)sizeof(addr);

    if (s == NULL || s->closing || (proto != ONCRPC_PROTO_TCP && proto != ONCRPC_PROTO_UDP))
        return ONCRPC_ERR_INVALID;
    if (ip == NULL)
        ip = "0.0.0.0";
    memset(&addr, 0, sizeof(addr));
    if (uv_ip4_addr(ip, port, (struct sockaddr_in *)&addr) != 0 &&
        uv_ip6_addr(ip, port, (struct sockaddr_in6 *)&addr) != 0)
        return ONCRPC_ERR_INVALID;

    l = (slistener *)oncRPCAlloc(s->alloc, sizeof(*l));
    if (l == NULL)
        return ONCRPC_ERR_NOMEM;
    l->srv = s;
    l->proto = proto;
    if (proto == ONCRPC_PROTO_UDP) {
        l->ubuf = (uint8_t *)oncRPCRealloc(s->alloc, NULL, ONCRPC_UDP_MAX);
        if (l->ubuf == NULL) {
            oncRPCFree(s->alloc, l);
            return ONCRPC_ERR_NOMEM;
        }
    }
    r = proto == ONCRPC_PROTO_TCP ? uv_tcp_init(s->loop, &l->io.tcp)
                                  : uv_udp_init(s->loop, &l->io.udp);
    if (r != 0) {
        oncRPCFree(s->alloc, l->ubuf);
        oncRPCFree(s->alloc, l);
        return oncRPCIntUvStat(r, ONCRPC_ERR_FAILED);
    }
    l->io.h.data = l;
    if (proto == ONCRPC_PROTO_TCP) {
        r = uv_tcp_bind(&l->io.tcp, (const struct sockaddr *)&addr, 0);
        if (r == 0)
            r = uv_listen((uv_stream_t *)&l->io.tcp, 128, on_connection);
        if (r == 0)
            r = uv_tcp_getsockname(&l->io.tcp, (struct sockaddr *)&addr, &namelen);
    } else {
        r = uv_udp_bind(&l->io.udp, (const struct sockaddr *)&addr, 0);
        if (r == 0)
            r = uv_udp_recv_start(&l->io.udp, on_udp_alloc, on_udp_recv);
        if (r == 0)
            r = uv_udp_getsockname(&l->io.udp, (struct sockaddr *)&addr, &namelen);
    }
    if (r != 0) {
        s->activeCloses++;
        uv_close(&l->io.h, on_listener_closed);
        return oncRPCIntUvStat(r, ONCRPC_ERR_FAILED);
    }
    memcpy(&l->bound, &addr, sizeof(addr));
    l->port = (uint16_t)(addr.ss_family == AF_INET6
                             ? port_of(&((struct sockaddr_in6 *)&addr)->sin6_port)
                             : port_of(&((struct sockaddr_in *)&addr)->sin_port));
    /* Append, so oncRPCServerPort reports the first listener. */
    if (s->listeners == NULL) {
        s->listeners = l;
    } else {
        slistener *t = s->listeners;
        while (t->next)
            t = t->next;
        t->next = l;
    }
    return ONCRPC_OK;
}

uint16_t oncRPCServerPort(const oncRPCServer *s, oncRPCProto proto)
{
    const slistener *l;
    if (s == NULL)
        return 0;
    for (l = s->listeners; l != NULL; l = l->next)
        if (l->proto == proto)
            return l->port;
    return 0;
}

oncRPCStat oncRPCServerRegister(oncRPCServer *s, uint32_t prog, uint32_t vers,
                                oncRPCDispatchFn fn, void *ctx)
{
    sreg *r;
    if (s == NULL || fn == NULL || s->closing)
        return ONCRPC_ERR_INVALID;
    for (r = s->regs; r != NULL; r = r->next)
        if (r->prog == prog && r->vers == vers)
            break;
    if (r == NULL) {
        r = (sreg *)oncRPCAlloc(s->alloc, sizeof(*r));
        if (r == NULL)
            return ONCRPC_ERR_NOMEM;
        r->prog = prog;
        r->vers = vers;
        r->next = s->regs;
        s->regs = r;
    }
    r->fn = fn;
    r->ctx = ctx;
    return ONCRPC_OK;
}

oncRPCStat oncRPCServerUnregister(oncRPCServer *s, uint32_t prog, uint32_t vers)
{
    sreg **pp;
    if (s == NULL)
        return ONCRPC_ERR_INVALID;
    for (pp = &s->regs; *pp != NULL; pp = &(*pp)->next) {
        sreg *r = *pp;
        if (r->prog == prog && r->vers == vers) {
            *pp = r->next;
            oncRPCFree(s->alloc, r);
            return ONCRPC_OK;
        }
    }
    return ONCRPC_ERR_INVALID;
}

void oncRPCServerSetMaxRecord(oncRPCServer *s, size_t bytes)
{
    if (s != NULL && bytes > 0)
        s->lim.maxRecord = bytes;
}

void oncRPCServerGetLimits(const oncRPCServer *s, oncRPCServerLimits *limits)
{
    if (s != NULL && limits != NULL)
        *limits = s->lim;
}

oncRPCStat oncRPCServerSetLimits(oncRPCServer *s, const oncRPCServerLimits *limits)
{
    if (s == NULL || limits == NULL || s->closing || limits->maxRecord == 0)
        return ONCRPC_ERR_INVALID;
    if (limits->duplicateCacheEntries != s->lim.duplicateCacheEntries ||
        limits->duplicateCacheBytes != s->lim.duplicateCacheBytes ||
        limits->duplicateCacheTtlMs != s->lim.duplicateCacheTtlMs)
        drc_clear(s);
    s->lim = *limits;
    restart_tick(s);
    schedule_resume(s);   /* raised request limits may free paused connections */
    return ONCRPC_OK;
}

void oncRPCServerGetStats(const oncRPCServer *s, oncRPCServerStats *stats)
{
    if (s == NULL || stats == NULL)
        return;
    *stats = s->stats;
    stats->connections = s->nconns;
    stats->pendingRequests = s->pendingReqs;
    stats->duplicateCacheEntries = s->drcEntries;
    stats->duplicateCacheBytes = s->drcBytes;
}

size_t oncRPCServerPending(const oncRPCServer *s)
{
    return s ? s->pendingReqs : 0;
}

const oncRPCAllocator *oncRPCServerAllocator(const oncRPCServer *s)
{
    return s ? s->alloc : NULL;
}

/* ---- Port mapper registration ------------------------------------------------------- */

typedef struct pmapJob {
    oncRPCServer      *srv;
    uint32_t           prog;
    uint32_t           vers;
    int                registering;
    size_t             outstanding;
    oncRPCStat         status;
    oncRPCServerPmapCb cb;
    void              *data;
} pmapJob;

static void job_done(pmapJob *j)
{
    oncRPCServer *s = j->srv;
    if (j->cb)
        j->cb(s, j->status, j->data);
    oncRPCFree(s->alloc, j);
    s->activeOps--;
    maybe_finish_close(s);
}

static void job_failed(pmapJob *j, oncRPCStat st)
{
    if (st != ONCRPC_OK && j->status == ONCRPC_OK)
        j->status = st;
}

static void on_pmap_set(oncRPCStat st, uint32_t rpcbVers, uint32_t ok, const char *uaddr,
                        void *data)
{
    pmapJob *j = (pmapJob *)data;
    (void)rpcbVers;
    (void)uaddr;
    job_failed(j, st == ONCRPC_OK && !ok ? ONCRPC_ERR_PMAPFAILURE : st);
    if (--j->outstanding == 0)
        job_done(j);
}

/* Stale entries are gone (or there were none), and rpcbVers is the protocol
 * version the daemon answered in: register each listener in it. */
static void on_pmap_unset(oncRPCStat st, uint32_t rpcbVers, uint32_t ok, const char *uaddr,
                          void *data)
{
    pmapJob *j = (pmapJob *)data;
    oncRPCServer *s = j->srv;
    slistener *l;
    (void)ok;     /* false just means there was nothing to remove */
    (void)uaddr;

    if (!j->registering || st != ONCRPC_OK) {
        /* Unregistering; or no daemon answered, so registering would only
         * wait for the same failure once per listener. */
        job_failed(j, st);
        job_done(j);
        return;
    }
    j->outstanding = 1;   /* guard while starting */
    for (l = s->listeners; l != NULL && !s->closing; l = l->next) {
        char uaddr_[ONCRPC_UADDR_MAX];
        const char *netid = oncRPCNetid(l->proto, l->bound.ss_family);
        oncRPCStat r;

        if (rpcbVers == ONCRPC_PMAP_VERS && l->bound.ss_family == AF_INET6) {
            /* The port mapper cannot describe IPv6 services. */
            job_failed(j, ONCRPC_ERR_PMAPFAILURE);
            continue;
        }
        if (netid == NULL ||
            oncRPCUaddrFromSockaddr((const struct sockaddr *)&l->bound, uaddr_,
                                    sizeof(uaddr_)) != 0) {
            job_failed(j, ONCRPC_ERR_FAILED);
            continue;
        }
        r = oncRPCIntRpcbStart(s->loop, s->alloc, "127.0.0.1", ONCRPC_RPCBPROC_SET, rpcbVers,
                               rpcbVers, j->prog, j->vers, netid, l->proto, uaddr_,
                               on_pmap_set, j, NULL);
        if (r == ONCRPC_OK)
            j->outstanding++;
        else
            job_failed(j, r);
    }
    if (s->listeners == NULL)
        job_failed(j, ONCRPC_ERR_INVALID);
    if (--j->outstanding == 0)
        job_done(j);
}

static oncRPCStat start_job(oncRPCServer *s, uint32_t prog, uint32_t vers, int registering,
                            oncRPCServerPmapCb cb, void *data)
{
    pmapJob *j;
    oncRPCStat st;

    if (s == NULL || s->closing)
        return ONCRPC_ERR_INVALID;
    if (registering && s->listeners == NULL)
        return ONCRPC_ERR_INVALID;
    j = (pmapJob *)oncRPCAlloc(s->alloc, sizeof(*j));
    if (j == NULL)
        return ONCRPC_ERR_NOMEM;
    j->srv = s;
    j->prog = prog;
    j->vers = vers;
    j->registering = registering;
    j->cb = cb;
    j->data = data;
    /* Remove the program version for every network id; this also finds the
     * newest protocol version the daemon speaks. */
    st = oncRPCIntRpcbStart(s->loop, s->alloc, "127.0.0.1", ONCRPC_RPCBPROC_UNSET,
                            ONCRPC_RPCB_VERS4, ONCRPC_PMAP_VERS, prog, vers, "",
                            ONCRPC_PROTO_TCP, "", on_pmap_unset, j, NULL);
    if (st != ONCRPC_OK) {
        oncRPCFree(s->alloc, j);
        return st;
    }
    s->activeOps++;
    return ONCRPC_OK;
}

oncRPCStat oncRPCServerPmapRegister(oncRPCServer *s, uint32_t prog, uint32_t vers,
                                    oncRPCServerPmapCb cb, void *data)
{
    return start_job(s, prog, vers, 1, cb, data);
}

oncRPCStat oncRPCServerPmapUnregister(oncRPCServer *s, uint32_t prog, uint32_t vers,
                                      oncRPCServerPmapCb cb, void *data)
{
    return start_job(s, prog, vers, 0, cb, data);
}
