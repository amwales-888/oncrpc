/*
 * client.c - asynchronous RPC client on libuv, over TCP (record marking),
 * UDP (with retransmission) and local sockets; TCP optionally inside TLS.
 *
 * Life of a client:
 *   [port mapper lookup] -> resolve -> connect -> [STARTTLS -> handshake] -> READY
 * Calls issued before READY are queued. A lost TCP connection fails the
 * calls already sent and is re-established when the next call is made.
 */
#include "internal.h"

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#  include <sys/types.h>
#  include <unistd.h>
#endif

typedef enum cstate {
    CS_PMAP,          /* asking the port mapper */
    CS_RESOLVING,     /* uv_getaddrinfo running */
    CS_IDLE,          /* address known, no connection (TCP) */
    CS_CONNECTING,
    CS_STARTTLS,      /* asked the server for TLS, waiting for its answer */
    CS_HANDSHAKE,     /* TLS handshake under way */
    CS_READY,
    CS_RESETTING,     /* closing a broken TCP handle */
    CS_FAILED,        /* resolution or port lookup failed for good */
    CS_CLOSING
} cstate;

typedef struct ccall {
    struct ccall  *next;
    struct ccall  *prev;
    uint32_t       xid;
    oncRPCCallId   id;         /* for oncRPCClientCancel */
    uint8_t       *msg;        /* encoded call, 4 bytes reserved for record mark */
    size_t         msglen;
    size_t         argOff;     /* where the arguments start in msg */
    uint32_t       proc;
    uint32_t       authGen;    /* the authentication it was encoded with */
    int            keepMsg;    /* keep msg after sending: may be refreshed */
    int            refreshes;
    oncRPCXdrProc  xres;
    void          *res;
    oncRPCCallCb   cb;
    void          *data;
    uint64_t       deadline;
    uint64_t       nextRetx;
    int            sent;
    oncRPCStat     fail;       /* set: complete with this status from the timer */
    int            failUv;
} ccall;

/* A write/send request owning a copy of the bytes being sent. */
typedef struct cwreq {
    union {
        uv_write_t    w;
        uv_udp_send_t u;
    } r;
    oncRPCClient *c;
    uint8_t      *buf;
} cwreq;

struct oncRPCClient {
    uv_loop_t             *loop;
    const oncRPCAllocator *alloc;
    char                  *host;
    uint16_t               port;
    oncRPCProto            proto;
    uint32_t               prog;
    uint32_t               vers;
    cstate                 state;
    oncRPCStat             failStat;

    struct sockaddr_storage addr;

    union {
        uv_handle_t h;
        uv_stream_t s;      /* TCP or local */
        uv_tcp_t    tcp;
        uv_pipe_t   pipe;
        uv_udp_t    udp;
    } io;
    int                    ioOpen;     /* io handle initialised and not closing */
    int                    ioClosing;  /* uv_close issued, callback pending */
    uv_timer_t             timer;
    uv_getaddrinfo_t       gai;
    int                    gaiActive;
    uv_connect_t           connReq;
    rpcbOp                *lookup;     /* asking rpcbind for the port */

    ccall                 *calls;      /* all outstanding calls, oldest first */
    ccall                 *callsTail;
    size_t                 ncalls;
    oncRPCCallId           lastId;     /* id of the most recently started call */
    uint32_t               xid;
    uint32_t               timeoutMs;
    uint32_t               retryMs;
    size_t                 maxRecord;
    oncRPCOpaqueAuth       cred;
    const oncRPCClientAuth *auth;      /* set: custom authentication */
    void                  *authData;
    uint32_t               authGen;    /* changes whenever auth is set */
    oncRPCError            err;

    uint8_t               *rbuf;       /* receive buffer */
    size_t                 rlen;
    size_t                 rcap;
    uint8_t               *mbuf;       /* reassembled TCP record */
    size_t                 mcap;

    oncRPCClientReadyCb    readyCb;
    void                  *data;
    int                    readyReported;
    int                    readyPending;  /* report deferred to the timer */
    oncRPCStat             readyStat;

    oncRPCCloseCb          closeCb;
    void                  *closeData;
    int                    closePending;

    /* TLS (RFC 9289) */
    oncRPCTlsConfig       *tlsCfg;     /* set: upgrade every TCP connection */
    char                  *tlsName;    /* what the server's certificate must name */
    tlsSession            *tls;        /* the current connection's session */
    uint8_t               *tbuf;       /* bytes as received, before decryption */
    uint32_t               probeXid;
    uint64_t               tlsDeadline;
    char                   tlsErr[192];
};

static void start_connect(oncRPCClient *c);
static void schedule(oncRPCClient *c);
static void send_call(oncRPCClient *c, ccall *k);
static void tls_failed(oncRPCClient *c, const char *why);
static int  tls_flush(oncRPCClient *c);
static void became_ready(oncRPCClient *c);
static void release_auth(oncRPCClient *c);

/* The connection is gone: so is its TLS session. */
static void drop_tls(oncRPCClient *c)
{
    oncRPCIntTlsFree(c->tls);
    c->tls = NULL;
}

/* ---- Call list ------------------------------------------------------------ */

static void link_call(oncRPCClient *c, ccall *k)
{
    k->next = NULL;
    k->prev = c->callsTail;
    if (c->callsTail)
        c->callsTail->next = k;
    else
        c->calls = k;
    c->callsTail = k;
    c->ncalls++;
}

static void unlink_call(oncRPCClient *c, ccall *k)
{
    if (k->prev)
        k->prev->next = k->next;
    else
        c->calls = k->next;
    if (k->next)
        k->next->prev = k->prev;
    else
        c->callsTail = k->prev;
    k->next = k->prev = NULL;
    c->ncalls--;
}

static void free_call(oncRPCClient *c, ccall *k)
{
    oncRPCFree(c->alloc, k->msg);
    oncRPCFree(c->alloc, k);
}

/* Keep the loop alive only while calls are outstanding. */
static void update_ref(oncRPCClient *c)
{
    if (!c->ioOpen)
        return;
    if (c->ncalls > 0)
        uv_ref(&c->io.h);
    else
        uv_unref(&c->io.h);
}

/* Complete a call that has already been unlinked. */
static void complete(oncRPCClient *c, ccall *k, oncRPCStat st)
{
    c->err.stat = st;
    if (k->cb)
        k->cb(c, st, k->res, k->data);
    free_call(c, k);
}

/*
 * Mark every call (or only those already sent) as failed with st. The
 * callbacks run from the timer, never from inside an API function.
 */
static void fail_calls(oncRPCClient *c, oncRPCStat st, int only_sent, int uverr)
{
    ccall *k;
    for (k = c->calls; k != NULL; k = k->next) {
        if (k->fail != ONCRPC_OK || (only_sent && !k->sent))
            continue;
        k->fail = st;
        k->failUv = uverr;
    }
    schedule(c);
}

static void report_ready(oncRPCClient *c, oncRPCStat st)
{
    if (c->readyReported || c->readyPending || c->state == CS_CLOSING)
        return;
    c->readyPending = 1;
    c->readyStat = st;
    schedule(c);
}

/* Calls that still need the connection. */
static int have_live_calls(const oncRPCClient *c)
{
    const ccall *k;
    for (k = c->calls; k != NULL; k = k->next)
        if (k->fail == ONCRPC_OK)
            return 1;
    return 0;
}

/* ---- Close ------------------------------------------------------------------ */

static void close_step_done(oncRPCClient *c)
{
    oncRPCCloseCb cb;
    void *data;
    ccall *k;

    if (--c->closePending > 0)
        return;
    /* Everything libuv knew about is closed: cancel remaining calls. */
    while ((k = c->calls) != NULL) {
        unlink_call(c, k);
        memset(&c->err, 0, sizeof(c->err));
        c->err.uvError = k->failUv;
        if (k->fail == ONCRPC_ERR_TLS)
            c->err.detail = c->tlsErr;
        complete(c, k, k->fail != ONCRPC_OK ? k->fail : ONCRPC_ERR_CANCELLED);
    }
    release_auth(c);
    cb = c->closeCb;
    data = c->closeData;
    oncRPCIntTlsFree(c->tls);
    oncRPCIntTlsConfigUnref(c->tlsCfg);
    oncRPCFree(c->alloc, c->tlsName);
    oncRPCFree(c->alloc, c->tbuf);
    oncRPCFree(c->alloc, c->host);
    oncRPCFree(c->alloc, c->rbuf);
    oncRPCFree(c->alloc, c->mbuf);
    oncRPCFree(c->alloc, c);
    if (cb)
        cb(data);
}

static void on_timer_closed(uv_handle_t *h)
{
    close_step_done((oncRPCClient *)h->data);
}

static void on_pmap_closed(void *data)
{
    close_step_done((oncRPCClient *)data);
}

static void on_io_closed(uv_handle_t *h)
{
    oncRPCClient *c = (oncRPCClient *)h->data;
    c->ioClosing = 0;
    if (c->state == CS_CLOSING) {
        close_step_done(c);
        return;
    }
    /* A broken connection has been torn down. */
    c->rlen = 0;
    c->state = CS_IDLE;
    if (have_live_calls(c))
        start_connect(c);
}

void oncRPCClientClose(oncRPCClient *c, oncRPCCloseCb cb, void *data)
{
    if (c == NULL || c->state == CS_CLOSING)
        return;
    c->state = CS_CLOSING;
    c->closeCb = cb;
    c->closeData = data;
    c->closePending = 1;   /* the timer */
    uv_close((uv_handle_t *)&c->timer, on_timer_closed);
    if (c->ioOpen) {
        c->ioOpen = 0;
        c->ioClosing = 1;
        uv_close(&c->io.h, on_io_closed);
    }
    if (c->ioClosing)
        c->closePending++;
    if (c->gaiActive) {
        c->closePending++;
        (void)uv_cancel((uv_req_t *)&c->gai);   /* callback still follows */
    }
    if (c->lookup != NULL) {
        c->closePending++;
        oncRPCIntRpcbCancel(c->lookup, on_pmap_closed, c);
        c->lookup = NULL;
    }
}

/* ---- Timer: timeouts and UDP retransmission ---------------------------------- */

static void on_timer(uv_timer_t *t)
{
    oncRPCClient *c = (oncRPCClient *)t->data;
    uint64_t now = uv_now(c->loop);
    ccall *done = NULL, *tail = NULL, *k, *next;

    if (c->readyPending) {
        c->readyPending = 0;
        c->readyReported = 1;
        memset(&c->err, 0, sizeof(c->err));
        c->err.stat = c->readyStat;
        if (c->readyStat == ONCRPC_ERR_TLS)
            c->err.detail = c->tlsErr;
        if (c->readyCb)
            c->readyCb(c, c->readyStat, c->data);
        if (c->state == CS_CLOSING)
            return;
    }
    if ((c->state == CS_STARTTLS || c->state == CS_HANDSHAKE) && now >= c->tlsDeadline)
        tls_failed(c, "no answer while setting up TLS");
    /* Collect finished calls first: callbacks may add or complete calls. */
    for (k = c->calls; k != NULL; k = next) {
        next = k->next;
        if (k->fail != ONCRPC_OK || k->deadline <= now) {
            unlink_call(c, k);
            k->next = NULL;
            if (tail)
                tail->next = k;
            else
                done = k;
            tail = k;
        }
    }
    for (k = done; k != NULL; k = next) {
        next = k->next;
        memset(&c->err, 0, sizeof(c->err));
        c->err.uvError = k->failUv;
        if (k->fail == ONCRPC_ERR_TLS)
            c->err.detail = c->tlsErr;
        complete(c, k, k->fail != ONCRPC_OK ? k->fail : ONCRPC_ERR_TIMEDOUT);
        if (c->state == CS_CLOSING) {
            /* The callback closed the client: hand the rest to close. */
            for (k = next; k != NULL; k = next) {
                next = k->next;
                link_call(c, k);
            }
            return;
        }
    }
    if (c->proto == ONCRPC_PROTO_UDP && c->state == CS_READY) {
        now = uv_now(c->loop);
        for (k = c->calls; k != NULL; k = k->next)
            if (k->sent && k->fail == ONCRPC_OK && k->nextRetx <= now)
                send_call(c, k);
    }
    update_ref(c);
    schedule(c);
}

static void schedule(oncRPCClient *c)
{
    uint64_t now, when = UINT64_MAX;
    ccall *k;

    if (c->state == CS_CLOSING)
        return;
    now = uv_now(c->loop);
    if (c->readyPending)
        when = now;
    if ((c->state == CS_STARTTLS || c->state == CS_HANDSHAKE) && c->tlsDeadline < when)
        when = c->tlsDeadline;
    for (k = c->calls; k != NULL && when > now; k = k->next) {
        if (k->fail != ONCRPC_OK)
            when = now;
        if (k->deadline < when)
            when = k->deadline;
        if (c->proto == ONCRPC_PROTO_UDP && k->sent && k->nextRetx < when)
            when = k->nextRetx;
    }
    if (when == UINT64_MAX) {
        uv_timer_stop(&c->timer);
        return;
    }
    uv_timer_start(&c->timer, on_timer, when > now ? when - now : 0, 0);
}

/* ---- Sending ----------------------------------------------------------------- */

static void on_tcp_written(uv_write_t *w, int status);
static void on_udp_sent(uv_udp_send_t *s, int status);

/* Close a broken TCP connection; calls already sent fail with st. */
static void reset_connection(oncRPCClient *c, oncRPCStat st, int uverr)
{
    if (c->state == CS_CLOSING || c->state == CS_RESETTING)
        return;
    drop_tls(c);
    if (c->ioOpen) {
        c->ioOpen = 0;
        c->ioClosing = 1;
        uv_close(&c->io.h, on_io_closed);
        c->state = CS_RESETTING;
    } else {
        c->state = CS_IDLE;
    }
    fail_calls(c, st, 1, uverr);
}

static void send_call(oncRPCClient *c, ccall *k)
{
    cwreq *w;
    uv_buf_t b;
    int r;
    size_t len = k->msglen - (c->proto != ONCRPC_PROTO_UDP ? 0 : 4);

    if (c->tls != NULL) {
        /* Encrypt; what OpenSSL produces is sent by tls_flush. */
        k->sent = 1;
        if (oncRPCIntTlsWrite(c->tls, k->msg, len) != 0 || !tls_flush(c)) {
            reset_connection(c, ONCRPC_ERR_CANTSEND, 0);
            return;
        }
        if (!k->keepMsg) {
            oncRPCFree(c->alloc, k->msg);
            k->msg = NULL;
        }
        return;
    }
    w = (cwreq *)oncRPCAlloc(c->alloc, sizeof(*w));
    if (w != NULL)
        w->buf = (uint8_t *)oncRPCRealloc(c->alloc, NULL, len);
    if (w == NULL || w->buf == NULL) {
        if (w != NULL)
            oncRPCFree(c->alloc, w);
        /* Leave it unsent; the timeout will report the failure. */
        return;
    }
    memcpy(w->buf, k->msg + (k->msglen - len), len);
    w->c = c;
    b = uv_buf_init((char *)w->buf, (unsigned int)len);
    if (c->proto != ONCRPC_PROTO_UDP) {
        r = uv_write(&w->r.w, &c->io.s, &b, 1, on_tcp_written);
    } else {
        r = uv_udp_send(&w->r.u, &c->io.udp, &b, 1, NULL, on_udp_sent);
        /* A failed datagram is simply retried at the next interval. */
        k->sent = 1;
        k->nextRetx = uv_now(c->loop) + c->retryMs;
    }
    if (r != 0) {
        oncRPCFree(c->alloc, w->buf);
        oncRPCFree(c->alloc, w);
        if (c->proto != ONCRPC_PROTO_UDP) {
            k->sent = 1;
            reset_connection(c, ONCRPC_ERR_CANTSEND, r);
        }
        return;
    }
    k->sent = 1;
    if (c->proto != ONCRPC_PROTO_UDP && !k->keepMsg) {
        /* Streams never retransmit: the encoded message is no longer needed. */
        oncRPCFree(c->alloc, k->msg);
        k->msg = NULL;
    }
}

static void on_tcp_written(uv_write_t *req, int status)
{
    cwreq *w = (cwreq *)req;
    oncRPCClient *c = w->c;
    oncRPCFree(c->alloc, w->buf);
    oncRPCFree(c->alloc, w);
    if (status < 0 && status != UV_ECANCELED)
        reset_connection(c, ONCRPC_ERR_CANTSEND, status);
}

static void on_udp_sent(uv_udp_send_t *req, int status)
{
    cwreq *w = (cwreq *)req;
    oncRPCClient *c = w->c;
    (void)status;   /* datagram loss is handled by retransmission */
    oncRPCFree(c->alloc, w->buf);
    oncRPCFree(c->alloc, w);
}

static void flush_queue(oncRPCClient *c)
{
    ccall *k, *next;
    for (k = c->calls; k != NULL && c->state == CS_READY; k = next) {
        next = k->next;
        if (!k->sent && k->fail == ONCRPC_OK)
            send_call(c, k);
    }
}

/* ---- Authentication and encoding -------------------------------------------------- */

static void release_auth(oncRPCClient *c)
{
    const oncRPCClientAuth *a = c->auth;
    void *d = c->authData;
    c->auth = NULL;
    c->authData = NULL;
    c->authGen++;
    if (a != NULL && a->release != NULL)
        a->release(d);
}

/* Encode the call header for xid/proc after the record mark: with the
 * client's AUTH_NONE/AUTH_SYS credential, or from its custom authentication. */
static int encode_header(oncRPCClient *c, oncRPCXdr *x, uint32_t xid, uint32_t proc)
{
    uint32_t mtype = ONCRPC_MSG_CALL, rpcvers = ONCRPC_PROTOCOL_VERSION;
    oncRPCAuthData a;
    size_t start;

    if (c->auth == NULL)
        return oncRPCIntEncodeCall(x, xid, c->prog, c->vers, proc, &c->cred);
    start = oncRPCXdrGetPos(x);
    memset(&a, 0, sizeof(a));
    if (c->auth->credential(c->authData, xid, proc, &a) != 0 ||
        a.len > ONCRPC_MAX_AUTH_BYTES)
        return 0;
    if (!oncRPCXdrUint32(x, &xid) || !oncRPCXdrUint32(x, &mtype) ||
        !oncRPCXdrUint32(x, &rpcvers) || !oncRPCXdrUint32(x, &c->prog) ||
        !oncRPCXdrUint32(x, &c->vers) || !oncRPCXdrUint32(x, &proc) ||
        !oncRPCXdrUint32(x, &a.flavor) || !oncRPCXdrUint32(x, &a.len) ||
        !oncRPCXdrOpaque(x, a.body, a.len))
        return 0;
    memset(&a, 0, sizeof(a));
    if (c->auth->verifier != NULL &&
        (c->auth->verifier(c->authData, x->buf + start, oncRPCXdrGetPos(x) - start, &a) != 0 ||
         a.len > ONCRPC_MAX_AUTH_BYTES))
        return 0;
    return oncRPCXdrUint32(x, &a.flavor) && oncRPCXdrUint32(x, &a.len) &&
           oncRPCXdrOpaque(x, a.body, a.len);
}

/*
 * The server refused a call's credentials and the authentication asked to
 * try again: encode it afresh with a new xid, keeping its arguments, and
 * queue it again. Returns 0 if that is not possible.
 */
static int resend_refreshed(oncRPCClient *c, ccall *k)
{
    oncRPCXdr x;
    uint32_t mark = 0, xid;
    uint8_t *msg;
    size_t len, argOff;

    if (k->msg == NULL || c->state == CS_CLOSING || c->state == CS_FAILED)
        return 0;
    if (!oncRPCXdrInitEncodeDynamic(&x, c->alloc, k->msglen + 64))
        return 0;
    xid = c->xid++;
    if (!oncRPCXdrUint32(&x, &mark) || !encode_header(c, &x, xid, k->proc) ||
        (argOff = oncRPCXdrGetPos(&x),
         !oncRPCXdrOpaque(&x, k->msg + k->argOff, (uint32_t)(k->msglen - k->argOff)))) {
        oncRPCXdrDestroy(&x);
        return 0;
    }
    msg = oncRPCXdrDetach(&x, &len);
    if (msg == NULL || (c->proto == ONCRPC_PROTO_UDP && len - 4 > ONCRPC_UDP_MAX)) {
        oncRPCFree(c->alloc, msg);
        return 0;
    }
    oncRPCIntPutBe32(msg, (uint32_t)(len - 4) | ONCRPC_LAST_FRAGMENT);
    oncRPCFree(c->alloc, k->msg);
    k->msg = msg;
    k->msglen = len;
    k->argOff = argOff;
    k->xid = xid;
    k->sent = 0;
    k->nextRetx = 0;
    k->refreshes++;
    link_call(c, k);
    if (c->state == CS_READY)
        send_call(c, k);
    else if (c->state == CS_IDLE)
        start_connect(c);
    return 1;
}

/* ---- Receiving ----------------------------------------------------------------- */

/* Decode the reply header and results; returns the call status. */
static oncRPCStat parse_reply(oncRPCClient *c, oncRPCXdr *x, ccall *k)
{
    uint32_t rstat, stat, why;
    oncRPCOpaqueAuth verf;

    if (!oncRPCXdrUint32(x, &rstat))
        return ONCRPC_ERR_CANTDECODE;
    if (rstat == ONCRPC_MSG_ACCEPTED) {
        if (!oncRPCIntXdrOpaqueAuth(x, &verf) || !oncRPCXdrUint32(x, &stat))
            return ONCRPC_ERR_CANTDECODE;
        if (c->auth != NULL && c->auth->validate != NULL && k->authGen == c->authGen) {
            /* The layouts of the two types are the same. */
            oncRPCAuthData v;
            v.flavor = verf.flavor;
            v.len = verf.len;
            memcpy(v.body, verf.body, verf.len);
            if (c->auth->validate(c->authData, k->xid, &v) != 0) {
                c->err.why = ONCRPC_AUTH_INVALIDRESP;
                return ONCRPC_ERR_AUTHERROR;
            }
        }
        switch (stat) {
        case ONCRPC_ACCEPT_SUCCESS:
            if (k->xres != NULL && !k->xres(x, k->res)) {
                oncRPCXdrFree(c->alloc, k->xres, k->res);
                return ONCRPC_ERR_CANTDECODE;
            }
            return ONCRPC_OK;
        case ONCRPC_ACCEPT_PROG_UNAVAIL:
            return ONCRPC_ERR_PROGUNAVAIL;
        case ONCRPC_ACCEPT_PROG_MISMATCH:
            if (!oncRPCXdrUint32(x, &c->err.low) || !oncRPCXdrUint32(x, &c->err.high))
                return ONCRPC_ERR_CANTDECODE;
            return ONCRPC_ERR_PROGVERSMISMATCH;
        case ONCRPC_ACCEPT_PROC_UNAVAIL:
            return ONCRPC_ERR_PROCUNAVAIL;
        case ONCRPC_ACCEPT_GARBAGE_ARGS:
            return ONCRPC_ERR_CANTDECODEARGS;
        case ONCRPC_ACCEPT_SYSTEM_ERR:
            return ONCRPC_ERR_SYSTEMERROR;
        default:
            return ONCRPC_ERR_FAILED;
        }
    }
    if (rstat == ONCRPC_MSG_DENIED) {
        if (!oncRPCXdrUint32(x, &stat))
            return ONCRPC_ERR_CANTDECODE;
        if (stat == ONCRPC_REJ_RPC_MISMATCH) {
            if (!oncRPCXdrUint32(x, &c->err.low) || !oncRPCXdrUint32(x, &c->err.high))
                return ONCRPC_ERR_CANTDECODE;
            return ONCRPC_ERR_VERSMISMATCH;
        }
        if (stat == ONCRPC_REJ_AUTH_ERROR) {
            if (!oncRPCXdrUint32(x, &why))
                return ONCRPC_ERR_CANTDECODE;
            c->err.why = (oncRPCAuthStat)why;
            return ONCRPC_ERR_AUTHERROR;
        }
    }
    return ONCRPC_ERR_CANTDECODE;
}

static void handle_reply(oncRPCClient *c, const uint8_t *buf, size_t len)
{
    oncRPCXdr x;
    uint32_t xid, mtype;
    oncRPCStat st;
    ccall *k;

    oncRPCXdrInitDecode(&x, c->alloc, buf, len);
    if (!oncRPCXdrUint32(&x, &xid) || !oncRPCXdrUint32(&x, &mtype) ||
        mtype != ONCRPC_MSG_REPLY)
        return;
    for (k = c->calls; k != NULL; k = k->next)
        if (k->xid == xid && k->sent && k->fail == ONCRPC_OK)
            break;
    if (k == NULL)
        return;   /* late or duplicate reply */
    unlink_call(c, k);
    memset(&c->err, 0, sizeof(c->err));
    st = parse_reply(c, &x, k);
    if (st == ONCRPC_ERR_AUTHERROR && c->err.why != ONCRPC_AUTH_INVALIDRESP &&
        c->auth != NULL && c->auth->refresh != NULL && k->authGen == c->authGen &&
        k->refreshes < 2 && c->auth->refresh(c->authData, c->err.why) &&
        resend_refreshed(c, k)) {
        update_ref(c);
        schedule(c);
        return;
    }
    complete(c, k, st);
    if (c->state != CS_CLOSING) {
        update_ref(c);
        schedule(c);
    }
}

static void on_alloc(uv_handle_t *h, size_t suggested, uv_buf_t *b)
{
    oncRPCClient *c = (oncRPCClient *)h->data;
    (void)suggested;
    if (c->proto == ONCRPC_PROTO_UDP) {
        *b = uv_buf_init((char *)c->rbuf, (unsigned int)c->rcap);
        return;
    }
    if (c->tls != NULL) {
        *b = uv_buf_init((char *)c->tbuf, ONCRPC_READ_CHUNK);
        return;
    }
    if (c->rcap - c->rlen < ONCRPC_READ_CHUNK &&
        (c->rlen > c->maxRecord + ONCRPC_READ_CHUNK ||
         !oncRPCIntGrow(c->alloc, &c->rbuf, &c->rcap, c->rlen + ONCRPC_READ_CHUNK))) {
        *b = uv_buf_init(NULL, 0);   /* reported as UV_ENOBUFS */
        return;
    }
    *b = uv_buf_init((char *)c->rbuf + c->rlen, (unsigned int)(c->rcap - c->rlen));
}

/* Hand the complete records in rbuf to handle_reply. */
static void process_records(oncRPCClient *c)
{
    size_t mlen;
    int r;
    while (c->state == CS_READY &&
           (r = oncRPCIntRecordExtract(c->alloc, c->rbuf, &c->rlen, c->maxRecord, &c->mbuf,
                                       &c->mcap, &mlen)) != 0) {
        if (r < 0) {
            reset_connection(c, ONCRPC_ERR_CANTRECV, 0);
            return;
        }
        handle_reply(c, c->mbuf, mlen);
    }
}

/* ---- TLS (RFC 9289) ----------------------------------------------------------- */

static void on_tls_written(uv_write_t *req, int status)
{
    cwreq *w = (cwreq *)req;
    oncRPCClient *c = w->c;
    oncRPCFree(c->alloc, w->buf);
    oncRPCFree(c->alloc, w);
    if (status < 0 && status != UV_ECANCELED) {
        if (c->state == CS_STARTTLS || c->state == CS_HANDSHAKE)
            tls_failed(c, "the connection failed while setting up TLS");
        else
            reset_connection(c, ONCRPC_ERR_CANTSEND, status);
    }
}

/* Write buf (len bytes, allocated; ownership passes) to the connection. */
static int write_raw(oncRPCClient *c, uint8_t *buf, size_t len)
{
    cwreq *w = (cwreq *)oncRPCAlloc(c->alloc, sizeof(*w));
    uv_buf_t b;
    if (w == NULL) {
        oncRPCFree(c->alloc, buf);
        return 0;
    }
    w->c = c;
    w->buf = buf;
    b = uv_buf_init((char *)buf, (unsigned int)len);
    if (uv_write(&w->r.w, &c->io.s, &b, 1, on_tls_written) != 0) {
        oncRPCFree(c->alloc, buf);
        oncRPCFree(c->alloc, w);
        return 0;
    }
    return 1;
}

/* Send whatever TLS has produced. Returns 0 on failure. */
static int tls_flush(oncRPCClient *c)
{
    uint8_t *buf;
    size_t len;
    int r;
    while (c->tls != NULL && (r = oncRPCIntTlsOutput(c->tls, c->alloc, &buf, &len)) != 0) {
        if (r < 0 || !write_raw(c, buf, len))
            return 0;
    }
    return 1;
}

/* TLS could not be set up on this connection: like a failed connection,
 * every waiting call fails (with ONCRPC_ERR_TLS); the next call tries
 * again on a new connection. */
static void tls_failed(oncRPCClient *c, const char *why)
{
    snprintf(c->tlsErr, sizeof(c->tlsErr), "%s", why);
    drop_tls(c);
    if (c->ioOpen) {
        c->ioOpen = 0;
        c->ioClosing = 1;
        uv_close(&c->io.h, on_io_closed);
        c->state = CS_RESETTING;
    } else {
        c->state = CS_IDLE;
    }
    report_ready(c, ONCRPC_ERR_TLS);
    fail_calls(c, ONCRPC_ERR_TLS, 0, 0);
}

/* Ask the server for TLS: a NULL call with the AUTH_TLS credential. */
static void start_tls(oncRPCClient *c)
{
    static const uint32_t tail[] = {
        0,                  /* procedure 0 */
        ONCRPC_AUTH_TLS, 0, /* credential: AUTH_TLS, empty */
        ONCRPC_AUTH_NONE, 0 /* verifier */
    };
    uint8_t *msg = (uint8_t *)oncRPCRealloc(c->alloc, NULL, 44);
    size_t i;

    if (msg == NULL) {
        tls_failed(c, "out of memory");
        return;
    }
    c->probeXid = c->xid++;
    oncRPCIntPutBe32(msg, 0x80000000u | 40u);   /* record mark: last fragment, 40 bytes */
    oncRPCIntPutBe32(msg + 4, c->probeXid);
    oncRPCIntPutBe32(msg + 8, ONCRPC_MSG_CALL);
    oncRPCIntPutBe32(msg + 12, ONCRPC_PROTOCOL_VERSION);
    oncRPCIntPutBe32(msg + 16, c->prog);
    oncRPCIntPutBe32(msg + 20, c->vers);
    for (i = 0; i < sizeof(tail) / sizeof(tail[0]); i++)
        oncRPCIntPutBe32(msg + 24 + 4 * i, tail[i]);
    c->state = CS_STARTTLS;
    c->tlsDeadline = uv_now(c->loop) + c->timeoutMs;
    if (!write_raw(c, msg, 44)) {
        tls_failed(c, "could not send the TLS request");
        return;
    }
    schedule(c);
}

/* Whether the reply agrees to TLS: accepted, SUCCESS, and the verifier
 * AUTH_NONE with the body "STARTTLS". Otherwise *why says what it was. */
static int starttls_agreed(oncRPCClient *c, const uint8_t *buf, size_t len, const char **why)
{
    oncRPCXdr x;
    oncRPCOpaqueAuth verf;
    uint32_t xid, mtype, rstat, astat;

    *why = "the server sent an unexpected answer to the TLS request";
    oncRPCXdrInitDecode(&x, c->alloc, buf, len);
    if (!oncRPCXdrUint32(&x, &xid) || !oncRPCXdrUint32(&x, &mtype) || xid != c->probeXid ||
        mtype != ONCRPC_MSG_REPLY || !oncRPCXdrUint32(&x, &rstat))
        return 0;
    if (rstat != ONCRPC_MSG_ACCEPTED) {
        *why = "the server refused TLS";
        return 0;
    }
    if (!oncRPCIntXdrOpaqueAuth(&x, &verf) || !oncRPCXdrUint32(&x, &astat))
        return 0;
    if (astat != ONCRPC_ACCEPT_SUCCESS || verf.flavor != ONCRPC_AUTH_NONE ||
        verf.len != ONCRPC_STARTTLS_LEN ||
        memcmp(verf.body, ONCRPC_STARTTLS, ONCRPC_STARTTLS_LEN) != 0) {
        *why = "the server does not offer TLS";
        return 0;
    }
    return 1;
}

/* Decrypt what has arrived and act on it. */
static void tls_pump(oncRPCClient *c)
{
    size_t got;
    int r;

    if (c->state == CS_HANDSHAKE) {
        r = oncRPCIntTlsHandshake(c->tls);
        if (!tls_flush(c)) {
            tls_failed(c, "could not send TLS data");
            return;
        }
        if (r < 0) {
            tls_failed(c, oncRPCIntTlsError(c->tls));
            return;
        }
        if (r == 0)
            return;
        became_ready(c);   /* sends the queued calls, encrypted */
    }
    while (c->state == CS_READY && c->tls != NULL) {
        if (c->rcap - c->rlen < ONCRPC_READ_CHUNK &&
            (c->rlen > c->maxRecord + ONCRPC_READ_CHUNK ||
             !oncRPCIntGrow(c->alloc, &c->rbuf, &c->rcap, c->rlen + ONCRPC_READ_CHUNK))) {
            reset_connection(c, ONCRPC_ERR_CANTRECV, UV_ENOBUFS);
            return;
        }
        r = oncRPCIntTlsRead(c->tls, c->rbuf + c->rlen, c->rcap - c->rlen, &got);
        if (r < 0) {
            /* The calls sent fail with the TLS reason - such as the server
             * refusing our certificate, which TLS 1.3 reports only after
             * the client's side of the handshake is done. */
            snprintf(c->tlsErr, sizeof(c->tlsErr), "%s", oncRPCIntTlsError(c->tls));
            reset_connection(c, ONCRPC_ERR_TLS, 0);
            return;
        }
        if (r == 0)
            break;
        c->rlen += got;
    }
    if (c->tls != NULL && !tls_flush(c)) {
        reset_connection(c, ONCRPC_ERR_CANTSEND, 0);
        return;
    }
    process_records(c);
}

/* The server agreed: start the handshake. */
static void begin_handshake(oncRPCClient *c)
{
    if (c->tbuf == NULL)
        c->tbuf = (uint8_t *)oncRPCRealloc(c->alloc, NULL, ONCRPC_READ_CHUNK);
    c->tls = c->tbuf != NULL ? oncRPCIntTlsStart(c->tlsCfg, c->tlsName) : NULL;
    if (c->tls == NULL) {
        tls_failed(c, "could not start TLS");
        return;
    }
    c->state = CS_HANDSHAKE;
    /* Nothing should follow the answer before the handshake, but pass on
     * whatever did. */
    if (c->rlen > 0 && oncRPCIntTlsFeed(c->tls, c->rbuf, c->rlen) != 0) {
        tls_failed(c, oncRPCIntTlsError(c->tls));
        return;
    }
    c->rlen = 0;
    tls_pump(c);
}

static void on_tcp_read(uv_stream_t *s, ssize_t nread, const uv_buf_t *b)
{
    oncRPCClient *c = (oncRPCClient *)s->data;
    size_t mlen;
    int r;
    (void)b;

    if (nread < 0) {
        if (c->state == CS_STARTTLS || c->state == CS_HANDSHAKE)
            tls_failed(c, "the server closed the connection while setting up TLS");
        else
            reset_connection(c, ONCRPC_ERR_CANTRECV, (int)nread);
        return;
    }
    if (nread == 0)
        return;
    if (c->tls != NULL) {
        if (oncRPCIntTlsFeed(c->tls, c->tbuf, (size_t)nread) != 0) {
            reset_connection(c, ONCRPC_ERR_CANTRECV, UV_ENOMEM);
            return;
        }
        tls_pump(c);
        return;
    }
    c->rlen += (size_t)nread;
    if (c->state == CS_STARTTLS) {
        const char *why;
        r = oncRPCIntRecordExtract(c->alloc, c->rbuf, &c->rlen, c->maxRecord, &c->mbuf, &c->mcap,
                                   &mlen);
        if (r < 0)
            tls_failed(c, "the server sent an unexpected answer to the TLS request");
        else if (r > 0 && !starttls_agreed(c, c->mbuf, mlen, &why))
            tls_failed(c, why);
        else if (r > 0)
            begin_handshake(c);
        return;
    }
    process_records(c);
}

static void on_udp_recv(uv_udp_t *u, ssize_t nread, const uv_buf_t *b,
                        const struct sockaddr *addr, unsigned flags)
{
    oncRPCClient *c = (oncRPCClient *)u->data;
    (void)addr;
    if (nread < 0) {
        /* e.g. ICMP port unreachable on the connected socket */
        fail_calls(c, ONCRPC_ERR_CANTRECV, 1, (int)nread);
        return;
    }
    if (nread == 0 || (flags & UV_UDP_PARTIAL))
        return;
    handle_reply(c, (const uint8_t *)b->base, (size_t)nread);
}

/* ---- Connecting ----------------------------------------------------------------- */

static void connect_failed(oncRPCClient *c, int uverr)
{
    oncRPCStat st = oncRPCIntUvStat(uverr, ONCRPC_ERR_CANTCONNECT);
    drop_tls(c);
    if (c->ioOpen) {
        c->ioOpen = 0;
        c->ioClosing = 1;
        uv_close(&c->io.h, on_io_closed);
        c->state = CS_RESETTING;
    } else {
        c->state = CS_IDLE;
    }
    report_ready(c, st);
    fail_calls(c, st, 0, uverr);
}

static void became_ready(oncRPCClient *c)
{
    c->state = CS_READY;
    update_ref(c);
    flush_queue(c);
    if (c->state == CS_READY)
        schedule(c);
    report_ready(c, ONCRPC_OK);
}

static void on_tcp_connected(uv_connect_t *req, int status)
{
    oncRPCClient *c = (oncRPCClient *)req->data;
    int r;
    if (c->state == CS_CLOSING)
        return;
    if (status < 0) {
        connect_failed(c, status);
        return;
    }
    r = uv_read_start(&c->io.s, on_alloc, on_tcp_read);
    if (r != 0) {
        connect_failed(c, r);
        return;
    }
    if (c->tlsCfg != NULL && c->proto == ONCRPC_PROTO_TCP)
        start_tls(c);
    else
        became_ready(c);
}

static void start_connect(oncRPCClient *c)
{
    int r;

    if (c->state != CS_IDLE || c->ioOpen || c->ioClosing)
        return;
    if (c->proto == ONCRPC_PROTO_LOCAL) {
        /* host is the socket's path (or the pipe's name on Windows). */
        r = uv_pipe_init(c->loop, &c->io.pipe, 0);
        if (r != 0) {
            connect_failed(c, r);
            return;
        }
        c->io.h.data = c;
        c->ioOpen = 1;
        c->connReq.data = c;
        c->state = CS_CONNECTING;
        uv_pipe_connect(&c->connReq, &c->io.pipe, c->host, on_tcp_connected);
        return;
    }
    if (c->proto == ONCRPC_PROTO_TCP) {
        r = uv_tcp_init(c->loop, &c->io.tcp);
        if (r != 0) {
            connect_failed(c, r);
            return;
        }
        c->io.h.data = c;
        c->ioOpen = 1;
        (void)uv_tcp_nodelay(&c->io.tcp, 1);
        c->connReq.data = c;
        c->state = CS_CONNECTING;
        r = uv_tcp_connect(&c->connReq, &c->io.tcp, (const struct sockaddr *)&c->addr,
                           on_tcp_connected);
        if (r != 0)
            connect_failed(c, r);
        return;
    }
    if (!oncRPCIntGrow(c->alloc, &c->rbuf, &c->rcap, ONCRPC_UDP_MAX)) {
        connect_failed(c, UV_ENOMEM);
        return;
    }
    r = uv_udp_init(c->loop, &c->io.udp);
    if (r != 0) {
        connect_failed(c, r);
        return;
    }
    c->io.h.data = c;
    c->ioOpen = 1;
    r = uv_udp_connect(&c->io.udp, (const struct sockaddr *)&c->addr);
    if (r == 0)
        r = uv_udp_recv_start(&c->io.udp, on_alloc, on_udp_recv);
    if (r != 0) {
        connect_failed(c, r);
        return;
    }
    became_ready(c);
}

static void resolution_failed(oncRPCClient *c, oncRPCStat st)
{
    c->state = CS_FAILED;
    c->failStat = st;
    report_ready(c, st);
    fail_calls(c, st, 0, 0);
}

static void on_resolved(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
    oncRPCClient *c = (oncRPCClient *)req->data;
    struct addrinfo *ai, *pick = NULL;

    c->gaiActive = 0;
    if (c->state == CS_CLOSING) {
        if (res)
            uv_freeaddrinfo(res);
        close_step_done(c);
        return;
    }
    if (status < 0 || res == NULL) {
        if (res)
            uv_freeaddrinfo(res);
        resolution_failed(c, oncRPCIntUvStat(status, ONCRPC_ERR_UNKNOWNHOST));
        return;
    }
    /* Prefer IPv4: port mappers and most servers listen there. */
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET && pick == NULL)
            pick = ai;
    }
    if (pick == NULL)
        pick = res;
    memset(&c->addr, 0, sizeof(c->addr));
    memcpy(&c->addr, pick->ai_addr, pick->ai_addrlen);
    uv_freeaddrinfo(res);
    c->state = CS_IDLE;
    start_connect(c);
}

static void start_resolve(oncRPCClient *c)
{
    struct addrinfo hints;
    char portstr[8];
    int r;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = c->proto == ONCRPC_PROTO_TCP ? SOCK_STREAM : SOCK_DGRAM;
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)c->port);
    c->gai.data = c;
    c->state = CS_RESOLVING;
    r = uv_getaddrinfo(c->loop, &c->gai, on_resolved, c->host, portstr, &hints);
    if (r != 0) {
        resolution_failed(c, oncRPCIntUvStat(r, ONCRPC_ERR_UNKNOWNHOST));
        return;
    }
    c->gaiActive = 1;
}

static void on_lookup_done(oncRPCStat st, uint32_t rpcbVers, uint32_t port, const char *uaddr,
                           void *data)
{
    oncRPCClient *c = (oncRPCClient *)data;
    (void)rpcbVers;
    (void)uaddr;   /* only the port is used: the host is the one we were given */
    c->lookup = NULL;
    if (st != ONCRPC_OK) {
        resolution_failed(c, st);
        return;
    }
    c->port = (uint16_t)port;
    start_resolve(c);
}

void oncRPCIntClientSetVersion(oncRPCClient *c, uint32_t vers)
{
    if (c != NULL)
        c->vers = vers;
}

int oncRPCIntClientFamily(const oncRPCClient *c)
{
    return c != NULL ? c->addr.ss_family : 0;
}

/* ---- Public API ------------------------------------------------------------------ */

static uint32_t initial_xid(oncRPCClient *c)
{
    uint64_t t = uv_hrtime();
    uint32_t v = (uint32_t)(t ^ (t >> 32));
    v ^= (uint32_t)(uintptr_t)c * 2654435761u;
    v ^= (uint32_t)uv_os_getpid() << 16;
    return v;
}

oncRPCStat oncRPCClientCreate(uv_loop_t *loop, const oncRPCAllocator *alloc, const char *host,
                              uint16_t port, uint32_t prog, uint32_t vers, oncRPCProto proto,
                              oncRPCClientReadyCb readyCb, void *data, oncRPCClient **out)
{
    oncRPCClient *c;
    oncRPCStat st;

    if (out != NULL)
        *out = NULL;
    if (loop == NULL || alloc == NULL || alloc->realloc == NULL || alloc->free == NULL ||
        host == NULL || out == NULL ||
        (proto != ONCRPC_PROTO_TCP && proto != ONCRPC_PROTO_UDP && proto != ONCRPC_PROTO_LOCAL))
        return ONCRPC_ERR_INVALID;
    c = (oncRPCClient *)oncRPCAlloc(alloc, sizeof(*c));
    if (c == NULL)
        return ONCRPC_ERR_NOMEM;
    c->loop = loop;
    c->alloc = alloc;
    c->host = oncRPCStrdup(alloc, host);
    if (c->host == NULL) {
        oncRPCFree(alloc, c);
        return ONCRPC_ERR_NOMEM;
    }
    c->port = port;
    c->proto = proto;
    c->prog = prog;
    c->vers = vers;
    c->xid = initial_xid(c);
    c->timeoutMs = ONCRPC_DEFAULT_TIMEOUT_MS;
    c->retryMs = ONCRPC_DEFAULT_RETRY_MS;
    c->maxRecord = ONCRPC_DEFAULT_MAX_RECORD;
    c->cred.flavor = ONCRPC_AUTH_NONE;
    c->readyCb = readyCb;
    c->data = data;
    if (uv_timer_init(loop, &c->timer) != 0) {
        oncRPCFree(alloc, c->host);
        oncRPCFree(alloc, c);
        return ONCRPC_ERR_FAILED;
    }
    c->timer.data = c;
    *out = c;

    if (proto == ONCRPC_PROTO_LOCAL) {
        /* A path needs neither name resolution nor a port. */
        c->state = CS_IDLE;
        start_connect(c);
        return ONCRPC_OK;
    }
    if (port != 0) {
        start_resolve(c);
        return ONCRPC_OK;
    }
    /* Ask rpcbind on the host first: GETADDR in version 4 or 3 (with the
     * netid for the family the host resolves to), else the port mapper's
     * GETPORT. */
    c->state = CS_PMAP;
    st = oncRPCIntRpcbStart(loop, alloc, host, ONCRPC_RPCBPROC_GETADDR, ONCRPC_RPCB_VERS4,
                            ONCRPC_PMAP_VERS, prog, vers, NULL, proto, NULL, on_lookup_done, c,
                            &c->lookup);
    if (st != ONCRPC_OK) {
        *out = NULL;
        c->readyCb = NULL;
        oncRPCClientClose(c, NULL, NULL);   /* releases the pmap client too */
        return st;
    }
    return ONCRPC_OK;
}

oncRPCStat oncRPCClientCall(oncRPCClient *c, uint32_t proc, oncRPCXdrProc xargs,
                            const void *args, oncRPCXdrProc xres, void *res,
                            oncRPCCallCb cb, void *data)
{
    oncRPCXdr x;
    ccall *k;
    uint32_t mark = 0;

    if (c == NULL || c->state == CS_CLOSING)
        return ONCRPC_ERR_INVALID;
    if (c->state == CS_FAILED)
        return c->failStat;
    k = (ccall *)oncRPCAlloc(c->alloc, sizeof(*k));
    if (k == NULL)
        return ONCRPC_ERR_NOMEM;
    if (!oncRPCXdrInitEncodeDynamic(&x, c->alloc, 512)) {
        oncRPCFree(c->alloc, k);
        return ONCRPC_ERR_NOMEM;
    }
    memset(k, 0, sizeof(*k));
    k->xid = c->xid++;
    k->proc = proc;
    k->authGen = c->authGen;
    k->keepMsg = c->auth != NULL && c->auth->refresh != NULL;
    (void)oncRPCXdrUint32(&x, &mark);   /* record mark, used only by TCP */
    if (!encode_header(c, &x, k->xid, proc) ||
        (k->argOff = oncRPCXdrGetPos(&x), xargs != NULL && !xargs(&x, (void *)args))) {
        int nomem = x.error && oncRPCXdrLength(&x) == 0;
        oncRPCXdrDestroy(&x);
        oncRPCFree(c->alloc, k);
        return nomem ? ONCRPC_ERR_NOMEM : ONCRPC_ERR_CANTENCODE;
    }
    k->msg = oncRPCXdrDetach(&x, &k->msglen);
    if (c->proto == ONCRPC_PROTO_UDP && k->msglen - 4 > ONCRPC_UDP_MAX) {
        free_call(c, k);
        return ONCRPC_ERR_CANTSEND;
    }
    oncRPCIntPutBe32(k->msg, (uint32_t)(k->msglen - 4) | ONCRPC_LAST_FRAGMENT);
    k->xres = xres;
    k->res = res;
    k->cb = cb;
    k->data = data;
    k->deadline = uv_now(c->loop) + c->timeoutMs;
    k->id = ++c->lastId;
    link_call(c, k);

    if (c->state == CS_READY)
        send_call(c, k);
    else if (c->state == CS_IDLE)
        start_connect(c);
    if (c->state != CS_CLOSING) {
        update_ref(c);
        schedule(c);
    }
    return ONCRPC_OK;
}

void oncRPCClientSetTimeout(oncRPCClient *c, uint32_t ms)
{
    if (c)
        c->timeoutMs = ms ? ms : 1;
}

void oncRPCClientSetRetry(oncRPCClient *c, uint32_t ms)
{
    if (c)
        c->retryMs = ms ? ms : 1;
}

void oncRPCClientSetMaxRecord(oncRPCClient *c, size_t bytes)
{
    if (c && bytes > 0)
        c->maxRecord = bytes;
}

oncRPCStat oncRPCClientSetTls(oncRPCClient *c, oncRPCTlsConfig *cfg, const char *serverName)
{
    char *name;

    if (c == NULL || cfg == NULL || oncRPCIntTlsConfigIsServer(cfg) ||
        c->proto != ONCRPC_PROTO_TCP)
        return ONCRPC_ERR_INVALID;
    /* Only before a connection is up (it is upgraded as soon as it is). */
    if (c->state != CS_PMAP && c->state != CS_RESOLVING && c->state != CS_IDLE &&
        c->state != CS_CONNECTING)
        return ONCRPC_ERR_INVALID;
    name = oncRPCStrdup(c->alloc, serverName != NULL ? serverName : c->host);
    if (name == NULL)
        return ONCRPC_ERR_NOMEM;
    oncRPCIntTlsConfigRef(cfg);
    oncRPCIntTlsConfigUnref(c->tlsCfg);
    oncRPCFree(c->alloc, c->tlsName);
    c->tlsCfg = cfg;
    c->tlsName = name;
    return ONCRPC_OK;
}

oncRPCCallId oncRPCClientLastCall(const oncRPCClient *c)
{
    return c ? c->lastId : 0;
}

oncRPCStat oncRPCClientCancel(oncRPCClient *c, oncRPCCallId id)
{
    ccall *k;

    if (c == NULL || id == 0)
        return ONCRPC_ERR_INVALID;
    for (k = c->calls; k != NULL; k = k->next) {
        if (k->id != id)
            continue;
        if (k->fail != ONCRPC_OK)
            return ONCRPC_ERR_INVALID;   /* already on its way out */
        k->fail = ONCRPC_ERR_CANCELLED;
        k->failUv = 0;
        if (c->state != CS_CLOSING)
            schedule(c);   /* closing completes it anyway */
        return ONCRPC_OK;
    }
    return ONCRPC_ERR_INVALID;
}

const oncRPCError *oncRPCClientLastError(const oncRPCClient *c)
{
    return c ? &c->err : NULL;
}

void *oncRPCClientData(const oncRPCClient *c)
{
    return c ? c->data : NULL;
}

const oncRPCAllocator *oncRPCClientAllocator(const oncRPCClient *c)
{
    return c ? c->alloc : NULL;
}

size_t oncRPCClientPending(const oncRPCClient *c)
{
    return c ? c->ncalls : 0;
}

oncRPCStat oncRPCClientSetAuthNone(oncRPCClient *c)
{
    if (c == NULL)
        return ONCRPC_ERR_INVALID;
    memset(&c->cred, 0, sizeof(c->cred));
    c->cred.flavor = ONCRPC_AUTH_NONE;
    release_auth(c);
    return ONCRPC_OK;
}

oncRPCStat oncRPCClientSetAuth(oncRPCClient *c, const oncRPCClientAuth *auth, void *data)
{
    if (c == NULL || auth == NULL || auth->credential == NULL || c->state == CS_CLOSING)
        return ONCRPC_ERR_INVALID;
    release_auth(c);
    c->auth = auth;
    c->authData = data;
    return ONCRPC_OK;
}

oncRPCStat oncRPCClientSetAuthSys(oncRPCClient *c, const char *machinename, uint32_t uid,
                                  uint32_t gid, uint32_t ngids, const uint32_t *gids)
{
    oncRPCAuthSysParams p;
    oncRPCXdr x;
    uint32_t i;

    if (c == NULL || ngids > ONCRPC_MAX_GIDS || (ngids > 0 && gids == NULL))
        return ONCRPC_ERR_INVALID;
    memset(&p, 0, sizeof(p));
    p.stamp = (uint32_t)(uv_hrtime() / 1000000000u);
    if (machinename != NULL) {
        size_t n = strlen(machinename);
        if (n > ONCRPC_MAX_MACHINENAME)
            n = ONCRPC_MAX_MACHINENAME;
        memcpy(p.machinename, machinename, n);
    }
    p.uid = uid;
    p.gid = gid;
    p.ngids = ngids;
    for (i = 0; i < ngids; i++)
        p.gids[i] = gids[i];
    oncRPCXdrInitEncode(&x, c->cred.body, sizeof(c->cred.body));
    if (!oncRPCXdrAuthSys(&x, &p))
        return ONCRPC_ERR_CANTENCODE;
    c->cred.flavor = ONCRPC_AUTH_SYS;
    c->cred.len = (uint32_t)oncRPCXdrLength(&x);
    release_auth(c);
    return ONCRPC_OK;
}

oncRPCStat oncRPCClientSetAuthSysDefault(oncRPCClient *c)
{
    char name[ONCRPC_MAX_MACHINENAME + 1];
    size_t namelen = sizeof(name);
    uint32_t gids[ONCRPC_MAX_GIDS];
    uint32_t ngids = 0, uid = 0, gid = 0;

    memset(name, 0, sizeof(name));
    memset(gids, 0, sizeof(gids));
    if (uv_os_gethostname(name, &namelen) != 0)
        memcpy(name, "localhost", sizeof("localhost"));
#ifndef _WIN32
    {
        gid_t groups[ONCRPC_MAX_GIDS];
        int n = getgroups(ONCRPC_MAX_GIDS, groups), i;
        uid = (uint32_t)getuid();
        gid = (uint32_t)getgid();
        for (i = 0; i < n && ngids < ONCRPC_MAX_GIDS; i++)
            gids[ngids++] = (uint32_t)groups[i];
    }
#endif
    return oncRPCClientSetAuthSys(c, name, uid, gid, ngids, gids);
}
