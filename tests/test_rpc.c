/*
 * test_rpc.c - client and server in one libuv loop.
 *
 *  1. functional: every calc procedure over TCP and UDP, many concurrent
 *     calls, deferred replies
 *  2. errors: unknown procedure/version, timeouts, cancellation on close,
 *     connection refused, unknown host
 *  3. lifecycle: server close waits for outstanding requests; a TCP client
 *     reconnects after the server restarts
 *  4. allocation: all memory returned; the out-of-memory sweep fails the
 *     n-th allocation for every n and checks the loop still winds down
 *     with nothing leaked
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calc.h"

static int g_fail, g_count;

#define CHECK(cond) do { g_count++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- Counting, optionally failing allocator --------------------------------- */

static long g_live, g_allocs, g_fail_at = -1;
static int g_fail_rest;   /* fail every allocation from g_fail_at on */

static void *t_realloc(void *ctx, void *p, size_t n)
{
    void *q;
    (void)ctx;
    if (p == NULL) {
        long serial = g_allocs++;
        if (g_fail_at >= 0 && (serial == g_fail_at || (g_fail_rest && serial > g_fail_at)))
            return NULL;   /* fail the n-th new block (and later ones) */
    }
    q = realloc(p, n);
    if (p == NULL && q != NULL)
        g_live++;
    return q;
}

static void t_free(void *ctx, void *p)
{
    (void)ctx;
    g_live--;
    free(p);
}

static const oncRPCAllocator A = { t_realloc, t_free, NULL };

static uv_loop_t g_loop;

/* ---- Server procedures --------------------------------------------------------- */

void oncRPCCalcNullV1Svc(oncRPCSvcReq *req)
{
    (void)oncRPCCalcNullV1Reply(req);
}

void oncRPCCalcComputeV1Svc(const calc_args *arg, oncRPCSvcReq *req)
{
    calc_result r;
    memset(&r, 0, sizeof(r));
    if (arg->op == OP_DIV && arg->b == 0) {
        r.status = 1;
        r.calc_result_u.error = "division by zero";
    } else {
        r.calc_result_u.value = arg->op == OP_ADD ? arg->a + arg->b
                              : arg->op == OP_SUB ? arg->a - arg->b
                              : arg->op == OP_MUL ? arg->a * arg->b : arg->a / arg->b;
    }
    (void)oncRPCCalcComputeV1Reply(req, &r);
}

void oncRPCCalcStatsV1Svc(const value_list *arg, oncRPCSvcReq *req)
{
    stats s;
    memset(&s, 0, sizeof(s));
    s.count = arg->value_list_len;
    (void)oncRPCCalcStatsV1Reply(req, &s);
}

/* CALC_ECHO replies from a timer after 5 ms. */
typedef struct echo_job {
    uv_timer_t    t;
    oncRPCSvcReq *req;
    const char   *text;
} echo_job;

static void on_echo_closed(uv_handle_t *h)
{
    oncRPCFree(&A, h->data);
}

static void on_echo_timer(uv_timer_t *t)
{
    echo_job *j = (echo_job *)t->data;
    (void)oncRPCCalcEchoV1Reply(j->req, j->text);
    uv_close((uv_handle_t *)t, on_echo_closed);
}

void oncRPCCalcEchoV1Svc(const char *arg, oncRPCSvcReq *req)
{
    echo_job *j = (echo_job *)oncRPCAlloc(&A, sizeof(*j));
    if (j == NULL) {
        oncRPCSvcErrSystem(req);
        return;
    }
    j->req = req;
    j->text = arg;
    uv_timer_init(&g_loop, &j->t);
    j->t.data = j;
    uv_timer_start(&j->t, on_echo_timer, 5, 0);
}

void oncRPCCalcReverseV1Svc(const name_list *arg, oncRPCSvcReq *req)
{
    name_node nodes[16];
    const name_node *n;
    name_list head = NULL;
    size_t i = 0;
    for (n = *arg; n != NULL && i < 16; n = n->next) {
        nodes[i].name = n->name;
        nodes[i].next = head;
        head = &nodes[i++];
    }
    (void)oncRPCCalcReverseV1Reply(req, &head);
}

/* CALC_ADD(x, -12345) is held (never answered) until release_held(). */
static oncRPCSvcReq *g_held[8];
static int32_t       g_heldSum[8];
static int g_nheld;
static int g_adds;   /* times CALC_ADD ran */

void oncRPCCalcAddV1Svc(const int32_t *a, const int32_t *b, oncRPCSvcReq *req)
{
    int32_t sum = *a + *b;
    g_adds++;
    if (*b == -12345 && g_nheld < 8) {
        g_heldSum[g_nheld] = sum;
        g_held[g_nheld++] = req;
        return;
    }
    (void)oncRPCCalcAddV1Reply(req, &sum);
}

/* Answer the held requests. */
static void reply_held(void)
{
    int i;
    for (i = 0; i < g_nheld; i++)
        (void)oncRPCCalcAddV1Reply(g_held[i], &g_heldSum[i]);
    g_nheld = 0;
}

static void release_held(void)
{
    int i;
    for (i = 0; i < g_nheld; i++)
        oncRPCSvcDrop(g_held[i]);
    g_nheld = 0;
}

/* ---- Helpers ------------------------------------------------------------------- */

static int g_closed;

static void on_closed(void *data)
{
    (void)data;
    g_closed++;
}

/* Run until nothing is left (only when no server is listening). */
static void run(void)
{
    uv_run(&g_loop, UV_RUN_DEFAULT);
}

/* Run until cond holds. */
#define WAIT(cond) do { while (!(cond)) uv_run(&g_loop, UV_RUN_ONCE); } while (0)

static oncRPCServer *start_server(uint16_t port)
{
    oncRPCServer *s = NULL;
    if (oncRPCServerCreate(&g_loop, &A, &s) != ONCRPC_OK)
        return NULL;
    if (oncRPCServerListen(s, ONCRPC_PROTO_TCP, "127.0.0.1", port) != ONCRPC_OK ||
        oncRPCServerListen(s, ONCRPC_PROTO_UDP, "127.0.0.1", port) != ONCRPC_OK ||
        oncRPCCalcProgV1Register(s, NULL) != ONCRPC_OK) {
        oncRPCServerClose(s, NULL, NULL);
        return NULL;
    }
    return s;
}

/* ---- 1. Functional ------------------------------------------------------------- */

#define NCONC 200

typedef struct fstate {
    int         ok;
    int         done;
    int         total;
    calc_result res[NCONC];
    char       *echo;
    name_list   rev;
    stats       st;
    int32_t     sum;
} fstate;

static void f_close_if_done(oncRPCClient *c, fstate *f)
{
    if (++f->done == f->total)
        oncRPCClientClose(c, on_closed, NULL);
}

static void f_compute(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    fstate *f = (fstate *)oncRPCClientData(c);
    calc_result *r = (calc_result *)res;
    intptr_t i = (intptr_t)data;
    if (st == ONCRPC_OK && r->status == 0 && r->calc_result_u.value == (int64_t)i * 3)
        f->ok++;
    if (st == ONCRPC_OK)
        oncRPCXdrFreeCalcResult(&A, r);
    f_close_if_done(c, f);
}

static void f_echo(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    fstate *f = (fstate *)oncRPCClientData(c);
    (void)data;
    if (st == ONCRPC_OK && strcmp(*(char **)res, "deferred") == 0)
        f->ok++;
    if (st == ONCRPC_OK)
        oncRPCFree(&A, *(char **)res);
    f_close_if_done(c, f);
}

static void f_reverse(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    fstate *f = (fstate *)oncRPCClientData(c);
    name_list l = *(name_list *)res;
    (void)data;
    if (st == ONCRPC_OK && l && strcmp(l->name, "b") == 0 && l->next &&
        strcmp(l->next->name, "a") == 0 && l->next->next == NULL)
        f->ok++;
    if (st == ONCRPC_OK)
        oncRPCXdrFreeNameList(&A, (name_list *)res);
    f_close_if_done(c, f);
}

static void f_stats(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    fstate *f = (fstate *)oncRPCClientData(c);
    (void)data;
    if (st == ONCRPC_OK && ((stats *)res)->count == 3)
        f->ok++;
    f_close_if_done(c, f);
}

static void f_add(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    fstate *f = (fstate *)oncRPCClientData(c);
    (void)data;
    if (st == ONCRPC_OK && *(int32_t *)res == 5)
        f->ok++;
    f_close_if_done(c, f);
}

static void functional(oncRPCProto proto, uint16_t port)
{
    static fstate f;
    oncRPCClient *c;
    int i;

    memset(&f, 0, sizeof(f));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "localhost", port, CALC_PROG, CALC_VERS, proto, NULL,
                             &f, &c) == ONCRPC_OK);
    oncRPCClientSetAuthSysDefault(c);
    for (i = 0; i < NCONC; i++) {
        calc_args a;
        a.op = OP_MUL;
        a.a = i;
        a.b = 3;
        if (oncRPCCalcComputeV1(c, &a, &f.res[i], f_compute, (void *)(intptr_t)i) == ONCRPC_OK)
            f.total++;
    }
    if (oncRPCCalcEchoV1(c, "deferred", &f.echo, f_echo, NULL) == ONCRPC_OK)
        f.total++;
    {
        name_node b = { "b", NULL }, a = { "a", &b };
        name_list l = &a;
        if (oncRPCCalcReverseV1(c, &l, &f.rev, f_reverse, NULL) == ONCRPC_OK)
            f.total++;
    }
    {
        double v[3] = { 1, 2, 3 };
        value_list l;
        l.value_list_len = 3;
        l.value_list_val = v;
        if (oncRPCCalcStatsV1(c, &l, &f.st, f_stats, NULL) == ONCRPC_OK)
            f.total++;
    }
    {
        int32_t x = 2, y = 3;
        if (oncRPCCalcAddV1(c, &x, &y, &f.sum, f_add, NULL) == ONCRPC_OK)
            f.total++;
    }
    CHECK(f.total == NCONC + 4);
    CHECK(oncRPCClientPending(c) == (size_t)f.total);
    WAIT(g_closed == 1);
    CHECK(f.ok == f.total);
    CHECK(g_closed == 1);
}

/* ---- 2. Errors -------------------------------------------------------------------- */

typedef struct estate {
    oncRPCStat stat;
    int        calls;
    oncRPCStat ready;
    int        readies;
    uint32_t   low, high;
} estate;

static void e_ready(oncRPCClient *c, oncRPCStat st, void *data)
{
    estate *e = (estate *)data;
    (void)c;
    e->ready = st;
    e->readies++;
}

static void e_call(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    estate *e = (estate *)data;
    (void)res;
    e->stat = st;
    e->calls++;
    e->low = oncRPCClientLastError(c)->low;
    e->high = oncRPCClientLastError(c)->high;
}

static void e_call_close(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    e_call(c, st, res, data);
    oncRPCClientClose(c, on_closed, NULL);
}

static void errors(oncRPCServer *srv, uint16_t port, uint16_t deadport)
{
    estate e;
    oncRPCClient *c;
    int32_t sum, x = 1, y = -12345;

    /* unknown procedure */
    memset(&e, 0, sizeof(e));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_TCP, NULL, NULL, &c) == ONCRPC_OK);
    CHECK(oncRPCClientCall(c, 42, NULL, NULL, NULL, NULL, e_call_close, &e) == ONCRPC_OK);
    WAIT(e.calls == 1 && g_closed == 1);
    CHECK(e.calls == 1 && e.stat == ONCRPC_ERR_PROCUNAVAIL);

    /* unknown version over UDP */
    memset(&e, 0, sizeof(e));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, CALC_PROG, 9, ONCRPC_PROTO_UDP,
                             NULL, NULL, &c) == ONCRPC_OK);
    CHECK(oncRPCClientCall(c, 0, NULL, NULL, NULL, NULL, e_call_close, &e) == ONCRPC_OK);
    WAIT(e.calls == 1 && g_closed == 1);
    CHECK(e.stat == ONCRPC_ERR_PROGVERSMISMATCH && e.low == 1 && e.high == 1);

    /* unknown program */
    memset(&e, 0, sizeof(e));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, 0x31234567, 1, ONCRPC_PROTO_TCP,
                             NULL, NULL, &c) == ONCRPC_OK);
    CHECK(oncRPCClientCall(c, 0, NULL, NULL, NULL, NULL, e_call_close, &e) == ONCRPC_OK);
    WAIT(e.calls == 1 && g_closed == 1);
    CHECK(e.stat == ONCRPC_ERR_PROGUNAVAIL);

    /* timeout: the server holds the request */
    memset(&e, 0, sizeof(e));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_TCP, NULL, NULL, &c) == ONCRPC_OK);
    oncRPCClientSetTimeout(c, 100);
    CHECK(oncRPCCalcAddV1(c, &x, &y, &sum, e_call_close, &e) == ONCRPC_OK);
    WAIT(e.calls == 1 && g_closed == 1);
    CHECK(e.stat == ONCRPC_ERR_TIMEDOUT);
    release_held();

    /* UDP timeout with retransmissions */
    memset(&e, 0, sizeof(e));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_UDP, NULL, NULL, &c) == ONCRPC_OK);
    oncRPCClientSetTimeout(c, 150);
    oncRPCClientSetRetry(c, 20);
    CHECK(oncRPCCalcAddV1(c, &x, &y, &sum, e_call_close, &e) == ONCRPC_OK);
    WAIT(e.calls == 1 && g_closed == 1);
    CHECK(e.stat == ONCRPC_ERR_TIMEDOUT);
    {
        /* The request was retransmitted, but the duplicate cache kept the
         * resends from running the procedure again. */
        oncRPCServerStats st;
        oncRPCServerGetStats(srv, &st);
        CHECK(g_nheld == 1);
        CHECK(st.duplicatesDropped > 0);
    }
    release_held();

    /* close with a call outstanding: cancelled */
    memset(&e, 0, sizeof(e));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_TCP, NULL, NULL, &c) == ONCRPC_OK);
    CHECK(oncRPCCalcAddV1(c, &x, &y, &sum, e_call, &e) == ONCRPC_OK);
    oncRPCClientClose(c, on_closed, NULL);
    CHECK(e.calls == 0);   /* never called from inside an API function */
    WAIT(g_closed == 1);
    CHECK(e.calls == 1 && e.stat == ONCRPC_ERR_CANCELLED && g_closed == 1);
    release_held();

    /* connection refused */
    memset(&e, 0, sizeof(e));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", deadport, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_TCP, e_ready, &e, &c) == ONCRPC_OK);
    CHECK(oncRPCClientCall(c, 0, NULL, NULL, NULL, NULL, e_call_close, &e) == ONCRPC_OK);
    WAIT(e.calls == 1 && g_closed == 1);
    WAIT(e.readies == 1);
    CHECK(e.readies == 1 && e.ready == ONCRPC_ERR_CANTCONNECT);
    CHECK(e.calls == 1 && e.stat == ONCRPC_ERR_CANTCONNECT);

    /* unknown host */
    memset(&e, 0, sizeof(e));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "no-such-host.invalid", 1, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_TCP, e_ready, &e, &c) == ONCRPC_OK);
    CHECK(oncRPCClientCall(c, 0, NULL, NULL, NULL, NULL, e_call_close, &e) == ONCRPC_OK);
    WAIT(e.calls == 1 && g_closed == 1);
    WAIT(e.readies == 1);
    CHECK(e.ready == ONCRPC_ERR_UNKNOWNHOST && e.stat == ONCRPC_ERR_UNKNOWNHOST);

    /* invalid arguments are reported synchronously */
    CHECK(oncRPCClientCreate(&g_loop, NULL, "x", 1, 1, 1, ONCRPC_PROTO_TCP, NULL, NULL, &c) ==
          ONCRPC_ERR_INVALID);
}

/* ---- 3. Lifecycle ---------------------------------------------------------------- */

static void lifecycle(uint16_t port)
{
    estate e;
    oncRPCClient *c;
    oncRPCServer *s;
    int32_t sum = 0, x = 1, y = -12345, z = 2;

    /* A server with an outstanding request finishes closing only after the
     * request is finished. */
    s = start_server(0);
    CHECK(s != NULL);
    memset(&e, 0, sizeof(e));
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", oncRPCServerPort(s, ONCRPC_PROTO_TCP),
                             CALC_PROG, CALC_VERS, ONCRPC_PROTO_TCP, NULL, NULL, &c) == ONCRPC_OK);
    CHECK(oncRPCCalcAddV1(c, &x, &y, &sum, e_call, &e) == ONCRPC_OK);
    while (g_nheld == 0)
        uv_run(&g_loop, UV_RUN_ONCE);
    CHECK(oncRPCServerPending(s) == 1);
    g_closed = 0;
    oncRPCServerClose(s, on_closed, NULL);
    uv_run(&g_loop, UV_RUN_NOWAIT);
    uv_run(&g_loop, UV_RUN_NOWAIT);
    CHECK(g_closed == 0);              /* still waiting for the request */
    release_held();
    while (g_closed == 0)
        uv_run(&g_loop, UV_RUN_ONCE);
    CHECK(g_closed == 1);
    while (e.calls == 0)               /* the client sees the connection go */
        uv_run(&g_loop, UV_RUN_ONCE);
    CHECK(e.stat == ONCRPC_ERR_CANTRECV);

    /* The TCP client reconnects when the server is back. */
    s = start_server(port);
    CHECK(s != NULL);
    CHECK(oncRPCClientCall(c, 0, NULL, NULL, NULL, NULL, e_call, &e) == ONCRPC_OK);
    e.calls = 0;
    while (e.calls == 0)
        uv_run(&g_loop, UV_RUN_ONCE);
    CHECK(e.stat == ONCRPC_ERR_CANTCONNECT);   /* old port: nobody listens */
    oncRPCClientClose(c, NULL, NULL);

    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_TCP, NULL, NULL, &c) == ONCRPC_OK);
    e.calls = 0;
    CHECK(oncRPCCalcAddV1(c, &x, &z, &sum, e_call, &e) == ONCRPC_OK);
    while (e.calls == 0)
        uv_run(&g_loop, UV_RUN_ONCE);
    CHECK(e.stat == ONCRPC_OK && sum == 3);
    /* Restart the server on the same port; the client reconnects by itself. */
    g_closed = 0;
    oncRPCServerClose(s, on_closed, NULL);
    while (g_closed == 0)
        uv_run(&g_loop, UV_RUN_ONCE);
    uv_run(&g_loop, UV_RUN_NOWAIT);    /* let the client notice the EOF */
    s = start_server(port);
    CHECK(s != NULL);
    e.calls = 0;
    sum = 0;
    CHECK(oncRPCCalcAddV1(c, &x, &z, &sum, e_call, &e) == ONCRPC_OK);
    while (e.calls == 0)
        uv_run(&g_loop, UV_RUN_ONCE);
    CHECK(e.stat == ONCRPC_OK && sum == 3);
    oncRPCClientClose(c, NULL, NULL);
    oncRPCServerClose(s, NULL, NULL);
    run();
}

/* ---- 4. Duplicate-request cache and limits ------------------------------------------ */

/* A bare UDP socket, so that requests can be resent byte for byte. */
static uv_udp_t g_raw;
static uint8_t  g_rawBuf[512];
static uint8_t  g_lastReply[512];
static size_t   g_lastReplyLen;
static int      g_replies;

static void raw_alloc(uv_handle_t *h, size_t suggested, uv_buf_t *b)
{
    (void)h;
    (void)suggested;
    *b = uv_buf_init((char *)g_rawBuf, (unsigned int)sizeof(g_rawBuf));
}

static void raw_recv(uv_udp_t *u, ssize_t n, const uv_buf_t *b, const struct sockaddr *a,
                     unsigned flags)
{
    (void)u;
    (void)a;
    (void)flags;
    if (n <= 0)
        return;
    memcpy(g_lastReply, b->base, (size_t)n);
    g_lastReplyLen = (size_t)n;
    g_replies++;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Send CALC_ADD(x, y) with AUTH_NONE as one datagram. */
static void raw_add(uint16_t port, uint32_t xid, int32_t x, int32_t y)
{
    uint32_t w[12];
    uint8_t msg[48];
    struct sockaddr_in to;
    uv_buf_t b;
    int i;

    w[0] = xid;
    w[1] = 0;            /* CALL */
    w[2] = 2;            /* RPC version */
    w[3] = CALC_PROG;
    w[4] = CALC_VERS;
    w[5] = CALC_ADD;
    w[6] = w[7] = 0;     /* credential: AUTH_NONE, empty */
    w[8] = w[9] = 0;     /* verifier */
    w[10] = (uint32_t)x;
    w[11] = (uint32_t)y;
    for (i = 0; i < 12; i++)
        put32(msg + 4 * i, w[i]);
    uv_ip4_addr("127.0.0.1", port, &to);
    b = uv_buf_init((char *)msg, (unsigned int)sizeof(msg));
    CHECK(uv_udp_try_send(&g_raw, &b, 1, (const struct sockaddr *)&to) == (int)sizeof(msg));
}

/* The sum in the last reply, which must be a successful one for xid. */
static int32_t last_sum(uint32_t xid)
{
    CHECK(g_lastReplyLen == 28 && get32(g_lastReply) == xid && get32(g_lastReply + 20) == 0);
    return (int32_t)get32(g_lastReply + 24);
}

static int g_slept;

static void on_sleep(uv_timer_t *t)
{
    (void)t;
    g_slept = 1;
}

/* Run the loop for about ms milliseconds. */
static void sleep_ms(uint64_t ms)
{
    uv_timer_t t;
    g_slept = 0;
    uv_timer_init(&g_loop, &t);
    uv_timer_start(&t, on_sleep, ms, 0);
    WAIT(g_slept);
    uv_close((uv_handle_t *)&t, NULL);
    uv_run(&g_loop, UV_RUN_NOWAIT);
}

/* One server statistic, for WAIT. */
static uint64_t stat_of(oncRPCServer *s, size_t offset)
{
    oncRPCServerStats st;
    oncRPCServerGetStats(s, &st);
    if (offset == offsetof(oncRPCServerStats, connections) ||
        offset == offsetof(oncRPCServerStats, duplicateCacheEntries))
        return *(const size_t *)(const void *)((const char *)&st + offset);
    return *(const uint64_t *)(const void *)((const char *)&st + offset);
}
#define STAT(s, field) stat_of((s), offsetof(oncRPCServerStats, field))

static void duplicates(void)
{
    oncRPCServer *s = start_server(0);
    oncRPCServerLimits lim;
    oncRPCServerStats st;
    struct sockaddr_in any;
    uint16_t port;
    int adds;

    CHECK(s != NULL);
    if (s == NULL)
        return;
    port = oncRPCServerPort(s, ONCRPC_PROTO_UDP);
    uv_udp_init(&g_loop, &g_raw);
    uv_ip4_addr("127.0.0.1", 0, &any);
    CHECK(uv_udp_bind(&g_raw, (const struct sockaddr *)&any, 0) == 0);
    CHECK(uv_udp_recv_start(&g_raw, raw_alloc, raw_recv) == 0);

    oncRPCServerGetLimits(s, &lim);
    CHECK(lim.duplicateCacheEntries == ONCRPC_DEFAULT_DRC_ENTRIES &&
          lim.maxRecord == ONCRPC_DEFAULT_MAX_RECORD);

    /* A resent request is answered from the cache with the same reply. */
    g_replies = 0;
    g_adds = 0;
    raw_add(port, 1, 2, 3);
    WAIT(g_replies == 1);
    CHECK(last_sum(1) == 5 && g_adds == 1);
    raw_add(port, 1, 2, 3);
    WAIT(g_replies == 2);
    CHECK(last_sum(1) == 5 && g_adds == 1);
    oncRPCServerGetStats(s, &st);
    CHECK(st.duplicatesReplayed == 1 && st.duplicateCacheEntries == 1 &&
          st.duplicateCacheBytes == 28);

    /* Same xid with different arguments, or a new xid: a new request. */
    raw_add(port, 1, 2, 4);
    WAIT(g_replies == 3);
    CHECK(last_sum(1) == 6 && g_adds == 2);
    raw_add(port, 2, 2, 3);
    WAIT(g_replies == 4);
    CHECK(last_sum(2) == 5 && g_adds == 3);

    /* Resends of a request still being handled are ignored, then answered
     * from the cache once the reply is sent. */
    raw_add(port, 3, 1, -12345);
    WAIT(g_nheld == 1);
    raw_add(port, 3, 1, -12345);
    raw_add(port, 3, 1, -12345);
    WAIT(STAT(s, duplicatesDropped) == 2);
    oncRPCServerGetStats(s, &st);
    CHECK(st.duplicatesDropped == 2 && g_nheld == 1 && g_adds == 4 && g_replies == 4);
    reply_held();
    WAIT(g_replies == 5);
    CHECK(last_sum(3) == -12344);
    raw_add(port, 3, 1, -12345);
    WAIT(g_replies == 6);
    CHECK(last_sum(3) == -12344 && g_adds == 4 && g_nheld == 0);

    /* A request dropped without a reply is forgotten: a resend runs again. */
    raw_add(port, 4, 1, -12345);
    WAIT(g_nheld == 1);
    release_held();
    raw_add(port, 4, 1, -12345);
    WAIT(g_nheld == 1);
    CHECK(g_adds == 6);
    release_held();

    /* Replies expire. */
    lim.duplicateCacheTtlMs = 40;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    oncRPCServerGetStats(s, &st);
    CHECK(st.duplicateCacheEntries == 0);   /* emptied by the change */
    adds = g_adds;
    raw_add(port, 5, 7, 8);
    WAIT(g_replies == 7);
    WAIT(STAT(s, duplicateCacheEntries) == 0);
    oncRPCServerGetStats(s, &st);
    CHECK(st.duplicateCacheEntries == 0 && st.duplicateCacheBytes == 0);
    raw_add(port, 5, 7, 8);
    WAIT(g_replies == 8);
    CHECK(last_sum(5) == 15 && g_adds == adds + 2);

    /* At most duplicateCacheEntries replies are kept; the oldest goes. */
    lim.duplicateCacheTtlMs = 0;   /* no expiry */
    lim.duplicateCacheEntries = 2;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    adds = g_adds;
    raw_add(port, 10, 1, 1);
    WAIT(g_replies == 9);
    raw_add(port, 11, 1, 1);
    WAIT(g_replies == 10);
    raw_add(port, 12, 1, 1);
    WAIT(g_replies == 11);
    oncRPCServerGetStats(s, &st);
    CHECK(st.duplicateCacheEntries == 2 && g_adds == adds + 3);
    raw_add(port, 12, 1, 1);           /* still cached */
    WAIT(g_replies == 12);
    CHECK(g_adds == adds + 3);
    raw_add(port, 10, 1, 1);           /* evicted: runs again */
    WAIT(g_replies == 13);
    CHECK(g_adds == adds + 4);

    /* ...and at most duplicateCacheBytes of them. */
    lim.duplicateCacheEntries = 100;
    lim.duplicateCacheBytes = 60;      /* two 28-byte replies */
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    raw_add(port, 20, 1, 1);
    WAIT(g_replies == 14);
    raw_add(port, 21, 1, 1);
    WAIT(g_replies == 15);
    raw_add(port, 22, 1, 1);
    WAIT(g_replies == 16);
    oncRPCServerGetStats(s, &st);
    CHECK(st.duplicateCacheEntries == 2 && st.duplicateCacheBytes == 56);

    /* Turned off, every resend runs again. */
    lim.duplicateCacheEntries = 0;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    adds = g_adds;
    raw_add(port, 30, 1, 1);
    WAIT(g_replies == 17);
    raw_add(port, 30, 1, 1);
    WAIT(g_replies == 18);
    CHECK(g_adds == adds + 2);
    oncRPCServerGetStats(s, &st);
    CHECK(st.duplicateCacheEntries == 0);

    /* With maxRequests reached, new UDP requests are dropped. */
    lim.maxRequests = 1;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    raw_add(port, 40, 1, -12345);
    WAIT(g_nheld == 1);
    raw_add(port, 41, 1, 1);
    WAIT(STAT(s, requestsDroppedBusy) == 1);
    oncRPCServerGetStats(s, &st);
    CHECK(st.requestsDroppedBusy == 1 && g_replies == 18 && st.pendingRequests == 1);
    reply_held();
    WAIT(g_replies == 19);
    raw_add(port, 41, 1, 1);
    WAIT(g_replies == 20);
    CHECK(last_sum(41) == 2);

    lim.maxRecord = 0;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_ERR_INVALID);

    uv_close((uv_handle_t *)&g_raw, NULL);
    g_closed = 0;
    oncRPCServerClose(s, on_closed, NULL);
    run();
    CHECK(g_closed == 1);
}

typedef struct lstate {
    int     calls;
    int     ok;
    int32_t sum[4];
} lstate;

static void l_call(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    lstate *l = (lstate *)data;
    (void)c;
    (void)res;
    l->calls++;
    l->ok += st == ONCRPC_OK;
}

static void limits(void)
{
    oncRPCServer *s = start_server(0);
    oncRPCServerLimits lim;
    oncRPCServerStats st;
    oncRPCClient *c1, *c2;
    lstate l;
    uint16_t port;
    int32_t x = 1, held = -12345, two = 2;
    int i;

    CHECK(s != NULL);
    if (s == NULL)
        return;
    port = oncRPCServerPort(s, ONCRPC_PROTO_TCP);
    oncRPCServerGetLimits(s, &lim);

    /* maxConnections: the second connection is closed at once. */
    lim.maxConnections = 1;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    memset(&l, 0, sizeof(l));
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_TCP, NULL, NULL, &c1) == ONCRPC_OK);
    CHECK(oncRPCCalcAddV1(c1, &x, &two, &l.sum[0], l_call, &l) == ONCRPC_OK);
    WAIT(l.calls == 1);
    CHECK(l.ok == 1 && l.sum[0] == 3);
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_TCP, NULL, NULL, &c2) == ONCRPC_OK);
    oncRPCClientSetTimeout(c2, 2000);
    CHECK(oncRPCCalcAddV1(c2, &x, &two, &l.sum[1], l_call, &l) == ONCRPC_OK);
    WAIT(l.calls == 2);
    CHECK(l.ok == 1);
    oncRPCServerGetStats(s, &st);
    CHECK(st.connectionsRefused >= 1 && st.connections == 1 && st.connectionsAccepted == 1);
    oncRPCClientClose(c2, NULL, NULL);

    /* maxRequestsPerConnection: reading stops at the limit and resumes as
     * requests finish; nothing is lost. */
    lim.maxConnections = 0;
    lim.maxRequestsPerConnection = 2;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    memset(&l, 0, sizeof(l));
    g_adds = 0;
    for (i = 0; i < 4; i++)
        CHECK(oncRPCCalcAddV1(c1, &x, &held, &l.sum[i], l_call, &l) == ONCRPC_OK);
    WAIT(g_nheld == 2 && STAT(s, connectionsPaused) >= 1);
    sleep_ms(20);
    oncRPCServerGetStats(s, &st);
    CHECK(g_nheld == 2 && g_adds == 2 && st.pendingRequests == 2 && st.connectionsPaused >= 1);
    reply_held();
    WAIT(g_nheld == 2);
    CHECK(g_adds == 4);
    reply_held();
    WAIT(l.calls == 4);
    CHECK(l.ok == 4);
    for (i = 0; i < 4; i++)
        CHECK(l.sum[i] == -12344);

    /* The global maxRequests pauses TCP connections too. */
    lim.maxRequestsPerConnection = 0;
    lim.maxRequests = 1;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    memset(&l, 0, sizeof(l));
    g_adds = 0;
    for (i = 0; i < 3; i++)
        CHECK(oncRPCCalcAddV1(c1, &x, &held, &l.sum[i], l_call, &l) == ONCRPC_OK);
    WAIT(g_nheld == 1);
    sleep_ms(20);
    CHECK(g_adds == 1);
    /* Raising the limit lets the waiting requests in. */
    lim.maxRequests = 0;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    WAIT(g_nheld == 3);
    reply_held();
    WAIT(l.calls == 3);
    CHECK(l.ok == 3);

    /* idleTimeoutMs: a quiet connection is closed; the client reconnects
     * for its next call. */
    lim.idleTimeoutMs = 40;
    CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
    WAIT(STAT(s, connectionsIdleClosed) == 1);
    sleep_ms(50);   /* let the client see the connection go */
    oncRPCServerGetStats(s, &st);
    CHECK(st.connectionsIdleClosed == 1 && st.connections == 0);
    memset(&l, 0, sizeof(l));
    CHECK(oncRPCCalcAddV1(c1, &x, &two, &l.sum[0], l_call, &l) == ONCRPC_OK);
    WAIT(l.calls == 1);
    CHECK(l.ok == 1 && l.sum[0] == 3);

    /* A connection with a request outstanding is not idle. */
    memset(&l, 0, sizeof(l));
    CHECK(oncRPCCalcAddV1(c1, &x, &held, &l.sum[0], l_call, &l) == ONCRPC_OK);
    WAIT(g_nheld == 1);
    sleep_ms(150);
    oncRPCServerGetStats(s, &st);
    CHECK(st.connections == 1);
    reply_held();
    WAIT(l.calls == 1);
    CHECK(l.ok == 1);

    oncRPCClientClose(c1, NULL, NULL);
    g_closed = 0;
    oncRPCServerClose(s, on_closed, NULL);
    run();
    CHECK(g_closed == 1);
}

/* ---- 5. Out-of-memory sweep -------------------------------------------------------- */

typedef struct ostate {
    int           pending;     /* clients still open */
    oncRPCServer *srv;
    uv_timer_t    guard;
} ostate;

typedef struct oclient {
    ostate      *o;
    int          outstanding;
    calc_result  res;
    char        *echo;
} oclient;

static void o_call(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    oclient *oc = (oclient *)oncRPCClientData(c);
    (void)st;
    (void)res;
    (void)data;
    if (--oc->outstanding > 0)
        return;
    oncRPCClientClose(c, NULL, NULL);
    if (--oc->o->pending == 0) {
        oncRPCServerClose(oc->o->srv, NULL, NULL);
        uv_close((uv_handle_t *)&oc->o->guard, NULL);
    }
}

static void o_compute(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    if (st == ONCRPC_OK)
        oncRPCXdrFreeCalcResult(&A, (calc_result *)res);
    o_call(c, st, res, data);
}

static void o_echo(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    if (st == ONCRPC_OK)
        oncRPCFree(&A, *(char **)res);
    o_call(c, st, res, data);
}

static void o_guard(uv_timer_t *t)
{
    (void)t;
    fprintf(stderr, "out-of-memory scenario did not finish\n");
    abort();
}

/* Returns the number of allocations the scenario made. */
static long oom_scenario(long fail_at)
{
    static ostate o;
    static oclient oc[2];
    int i;

    memset(&o, 0, sizeof(o));
    memset(oc, 0, sizeof(oc));
    g_allocs = 0;
    g_fail_at = fail_at;
    uv_timer_init(&g_loop, &o.guard);
    uv_timer_start(&o.guard, o_guard, 10000, 0);
    o.srv = start_server(0);
    if (o.srv != NULL) {
        for (i = 0; i < 2; i++) {
            oncRPCProto p = i == 0 ? ONCRPC_PROTO_TCP : ONCRPC_PROTO_UDP;
            oncRPCClient *c;
            calc_args a;
            a.op = OP_ADD;
            a.a = 1;
            a.b = 2;
            oc[i].o = &o;
            if (oncRPCClientCreate(&g_loop, &A, "127.0.0.1", oncRPCServerPort(o.srv, p),
                                   CALC_PROG, CALC_VERS, p, NULL, &oc[i], &c) != ONCRPC_OK)
                continue;
            oncRPCClientSetTimeout(c, 300);
            oncRPCClientSetRetry(c, 50);
            oc[i].outstanding += oncRPCCalcComputeV1(c, &a, &oc[i].res, o_compute, NULL) == ONCRPC_OK;
            oc[i].outstanding += oncRPCCalcEchoV1(c, "x", &oc[i].echo, o_echo, NULL) == ONCRPC_OK;
            if (oc[i].outstanding == 0)
                oncRPCClientClose(c, NULL, NULL);
            else
                o.pending++;
        }
    }
    if (o.pending == 0) {
        if (o.srv != NULL)
            oncRPCServerClose(o.srv, NULL, NULL);
        uv_close((uv_handle_t *)&o.guard, NULL);
    }
    run();
    g_fail_at = -1;
    return g_allocs;
}

static void oom_sweep(int rest)
{
    long total, n, failures = 0;

    g_fail_rest = rest;
    total = oom_scenario(-1);
    CHECK(total > 20);
    CHECK(g_live == 0);
    for (n = 0; n <= total; n++) {
        oom_scenario(n);
        if (g_live != 0) {
            fprintf(stderr, "out-of-memory at allocation %ld leaked %ld blocks\n", n, g_live);
            failures++;
            g_live = 0;
        }
    }
    g_fail_rest = 0;
    g_count++;
    if (failures)
        g_fail++;
    printf("  out-of-memory sweep (%s): %ld allocation points, %ld leaking\n",
           rest ? "every allocation from n on fails" : "allocation n fails", total + 1, failures);
}

/* ---- main ------------------------------------------------------------------------ */

int main(void)
{
    oncRPCServer *s;
    uint16_t port, deadport;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    CHECK(uv_loop_init(&g_loop) == 0);

    /* Find two free ports: one to serve on, one that stays closed. */
    s = start_server(0);
    CHECK(s != NULL);
    if (s == NULL)
        return 1;
    port = oncRPCServerPort(s, ONCRPC_PROTO_TCP);
    {
        oncRPCServer *t = start_server(0);
        deadport = oncRPCServerPort(t, ONCRPC_PROTO_TCP);
        oncRPCServerClose(t, NULL, NULL);
    }
    /* Restart so that the UDP listener has the same port number as TCP. */
    oncRPCServerClose(s, NULL, NULL);
    run();
    s = start_server(port);
    CHECK(s != NULL);

    printf("  functional over TCP\n");
    functional(ONCRPC_PROTO_TCP, port);
    printf("  functional over UDP\n");
    functional(ONCRPC_PROTO_UDP, port);
    printf("  error reporting\n");
    errors(s, port, deadport);
    oncRPCServerClose(s, NULL, NULL);
    run();
    CHECK(g_live == 0);

    printf("  lifecycle\n");
    lifecycle(port);
    CHECK(g_live == 0);

    printf("  duplicate-request cache\n");
    duplicates();
    CHECK(g_live == 0);
    printf("  server limits\n");
    limits();
    CHECK(g_live == 0);

    oom_sweep(0);
    oom_sweep(1);

    CHECK(uv_loop_close(&g_loop) == 0);
    CHECK(g_live == 0);
    printf("test_rpc: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
