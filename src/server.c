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
    int                     closing;
    struct sconn           *next;
    struct sconn           *prev;
} sconn;

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
    size_t                 maxRecord;
    size_t                 pendingReqs;   /* dispatched, not finished */
    size_t                 activeCloses;  /* uv_close callbacks outstanding */
    size_t                 activeOps;     /* writes/sends and port mapper ops */
    uint8_t               *msg;           /* reassembled TCP record */
    size_t                 msgcap;
    int                    closing;
    oncRPCCloseCb          closeCb;
    void                  *closeData;
};

#define ONCRPC_REQ(r) ((svcCall *)(void *)(r))

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
    if (r->conn != NULL) {
        if (r->prev)
            r->prev->next = r->next;
        else
            r->conn->reqs = r->next;
        if (r->next)
            r->next->prev = r->prev;
    }
    oncRPCFree(s->alloc, r);
    s->pendingReqs--;
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

static void handle_call(oncRPCServer *s, const uint8_t *msg, size_t len, oncRPCProto proto,
                        sconn *conn, slistener *lis, const struct sockaddr_storage *peer)
{
    svcCall *r;
    oncRPCXdr x;
    oncRPCOpaqueAuth cred, verf;
    uint32_t mtype, rpcvers, low = UINT32_MAX, high = 0;
    sreg *g, *match = NULL;
    int found_prog = 0;

    if (s->closing)
        return;
    oncRPCXdrInitDecode(&x, s->alloc, msg, len);
    {
        uint32_t xid;
        if (!oncRPCXdrUint32(&x, &xid) || !oncRPCXdrUint32(&x, &mtype) ||
            mtype != ONCRPC_MSG_CALL || !oncRPCXdrUint32(&x, &rpcvers))
            return;   /* not a call: ignore silently */
        r = (svcCall *)oncRPCAlloc(s->alloc, sizeof(*r));
        if (r == NULL)
            return;   /* no memory: the client will retry or time out */
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
    }
    s->pendingReqs++;

    if (rpcvers != ONCRPC_PROTOCOL_VERSION) {
        reply_denied(r, ONCRPC_REJ_RPC_MISMATCH, ONCRPC_PROTOCOL_VERSION,
                     ONCRPC_PROTOCOL_VERSION);
        return;
    }
    if (!oncRPCXdrUint32(&x, &r->pub.prog) || !oncRPCXdrUint32(&x, &r->pub.vers) ||
        !oncRPCXdrUint32(&x, &r->pub.proc) || !oncRPCIntXdrOpaqueAuth(&x, &cred) ||
        !oncRPCIntXdrOpaqueAuth(&x, &verf)) {
        reply_accepted(r, ONCRPC_ACCEPT_GARBAGE_ARGS, 0, 0);
        return;
    }
    r->pub.credFlavor = cred.flavor;
    if (cred.flavor == ONCRPC_AUTH_SYS) {
        oncRPCXdr ax;
        oncRPCXdrInitDecode(&ax, NULL, cred.body, cred.len);
        if (!oncRPCXdrAuthSys(&ax, &r->sys)) {
            reply_denied(r, ONCRPC_REJ_AUTH_ERROR, ONCRPC_AUTH_BADCRED, 0);
            return;
        }
        r->pub.authSys = &r->sys;
    } else if (cred.flavor != ONCRPC_AUTH_NONE) {
        reply_denied(r, ONCRPC_REJ_AUTH_ERROR, ONCRPC_AUTH_REJECTEDCRED, 0);
        return;
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
        return;
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
        (c->len > s->maxRecord + ONCRPC_READ_CHUNK ||
         !oncRPCIntGrow(s->alloc, &c->buf, &c->cap, c->len + ONCRPC_READ_CHUNK))) {
        *b = uv_buf_init(NULL, 0);   /* reported as UV_ENOBUFS: connection dropped */
        return;
    }
    *b = uv_buf_init((char *)c->buf + c->len, (unsigned int)(c->cap - c->len));
}

static void on_conn_read(uv_stream_t *st, ssize_t nread, const uv_buf_t *b)
{
    sconn *c = (sconn *)st->data;
    oncRPCServer *s = c->srv;
    size_t mlen;
    int r;
    (void)b;

    if (nread < 0) {
        conn_close(c);
        return;
    }
    c->len += (size_t)nread;
    while (!c->closing && !s->closing &&
           (r = oncRPCIntRecordExtract(s->alloc, c->buf, &c->len, s->maxRecord, &s->msg,
                                       &s->msgcap, &mlen)) != 0) {
        if (r < 0) {
            conn_close(c);
            return;
        }
        handle_call(s, s->msg, mlen, ONCRPC_PROTO_TCP, c, NULL, &c->peer);
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
    c->next = s->conns;
    if (s->conns)
        s->conns->prev = c;
    s->conns = c;
    if (uv_accept(server, (uv_stream_t *)&c->tcp) != 0) {
        conn_close(c);
        return;
    }
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
    struct sockaddr_storage peer;

    if (nread <= 0 || addr == NULL || (flags & UV_UDP_PARTIAL))
        return;
    memset(&peer, 0, sizeof(peer));
    memcpy(&peer, addr, addr->sa_family == AF_INET6 ? sizeof(struct sockaddr_in6)
                                                    : sizeof(struct sockaddr_in));
    handle_call(l->srv, (const uint8_t *)b->base, (size_t)nread, ONCRPC_PROTO_UDP, NULL, l,
                &peer);
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
    s->maxRecord = ONCRPC_DEFAULT_MAX_RECORD;
    *out = s;
    return ONCRPC_OK;
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
        s->maxRecord = bytes;
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

static void on_pmap_set(oncRPCStat st, uint32_t ok, void *data)
{
    pmapJob *j = (pmapJob *)data;
    if (st == ONCRPC_OK && !ok)
        st = ONCRPC_ERR_PMAPFAILURE;
    if (st != ONCRPC_OK && j->status == ONCRPC_OK)
        j->status = st;
    if (--j->outstanding == 0)
        job_done(j);
}

static void on_pmap_unset(oncRPCStat st, uint32_t ok, void *data)
{
    pmapJob *j = (pmapJob *)data;
    oncRPCServer *s = j->srv;
    slistener *l;
    (void)ok;

    if (!j->registering) {
        j->status = st;
        job_done(j);
        return;
    }
    /* Stale entries are gone (or there were none); register each listener. */
    j->outstanding = 1;   /* guard while starting */
    for (l = s->listeners; l != NULL && !s->closing; l = l->next) {
        oncRPCStat r = oncRPCPmapSet(s->loop, s->alloc, j->prog, j->vers, l->proto, l->port,
                                     on_pmap_set, j);
        if (r == ONCRPC_OK)
            j->outstanding++;
        else if (j->status == ONCRPC_OK)
            j->status = r;
    }
    if (s->listeners == NULL && j->status == ONCRPC_OK)
        j->status = ONCRPC_ERR_INVALID;
    if (st != ONCRPC_OK && j->status == ONCRPC_OK && j->outstanding == 1)
        j->status = st;
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
    st = oncRPCPmapUnset(s->loop, s->alloc, prog, vers, on_pmap_unset, j);
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
