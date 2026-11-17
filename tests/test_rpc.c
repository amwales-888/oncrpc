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

void calc_null_1_svc(oncRPCSvcReq *req)
{
    (void)calc_null_1_reply(req);
}

void calc_compute_1_svc(const calc_args *arg, oncRPCSvcReq *req)
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
    (void)calc_compute_1_reply(req, &r);
}

void calc_stats_1_svc(const value_list *arg, oncRPCSvcReq *req)
{
    stats s;
    memset(&s, 0, sizeof(s));
    s.count = arg->value_list_len;
    (void)calc_stats_1_reply(req, &s);
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
    (void)calc_echo_1_reply(j->req, j->text);
    uv_close((uv_handle_t *)t, on_echo_closed);
}

void calc_echo_1_svc(const char *arg, oncRPCSvcReq *req)
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

void calc_reverse_1_svc(const name_list *arg, oncRPCSvcReq *req)
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
    (void)calc_reverse_1_reply(req, &head);
}

/* CALC_ADD(x, -12345) is held (never answered) until release_held(). */
static oncRPCSvcReq *g_held[8];
static int g_nheld;

void calc_add_1_svc(const int32_t *a, const int32_t *b, oncRPCSvcReq *req)
{
    int32_t sum = *a + *b;
    if (*b == -12345 && g_nheld < 8) {
        g_held[g_nheld++] = req;
        return;
    }
    (void)calc_add_1_reply(req, &sum);
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
        calc_prog_1_register(s, NULL) != ONCRPC_OK) {
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
        xdr_free_calc_result(&A, r);
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
        xdr_free_name_list(&A, (name_list *)res);
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
        if (calc_compute_1(c, &a, &f.res[i], f_compute, (void *)(intptr_t)i) == ONCRPC_OK)
            f.total++;
    }
    if (calc_echo_1(c, "deferred", &f.echo, f_echo, NULL) == ONCRPC_OK)
        f.total++;
    {
        name_node b = { "b", NULL }, a = { "a", &b };
        name_list l = &a;
        if (calc_reverse_1(c, &l, &f.rev, f_reverse, NULL) == ONCRPC_OK)
            f.total++;
    }
    {
        double v[3] = { 1, 2, 3 };
        value_list l;
        l.value_list_len = 3;
        l.value_list_val = v;
        if (calc_stats_1(c, &l, &f.st, f_stats, NULL) == ONCRPC_OK)
            f.total++;
    }
    {
        int32_t x = 2, y = 3;
        if (calc_add_1(c, &x, &y, &f.sum, f_add, NULL) == ONCRPC_OK)
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

static void errors(uint16_t port, uint16_t deadport)
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
    CHECK(calc_add_1(c, &x, &y, &sum, e_call_close, &e) == ONCRPC_OK);
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
    CHECK(calc_add_1(c, &x, &y, &sum, e_call_close, &e) == ONCRPC_OK);
    WAIT(e.calls == 1 && g_closed == 1);
    CHECK(e.stat == ONCRPC_ERR_TIMEDOUT);
    CHECK(g_nheld > 1);   /* the request was retransmitted */
    release_held();

    /* close with a call outstanding: cancelled */
    memset(&e, 0, sizeof(e));
    g_closed = 0;
    CHECK(oncRPCClientCreate(&g_loop, &A, "127.0.0.1", port, CALC_PROG, CALC_VERS,
                             ONCRPC_PROTO_TCP, NULL, NULL, &c) == ONCRPC_OK);
    CHECK(calc_add_1(c, &x, &y, &sum, e_call, &e) == ONCRPC_OK);
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
    CHECK(calc_add_1(c, &x, &y, &sum, e_call, &e) == ONCRPC_OK);
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
    CHECK(calc_add_1(c, &x, &z, &sum, e_call, &e) == ONCRPC_OK);
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
    CHECK(calc_add_1(c, &x, &z, &sum, e_call, &e) == ONCRPC_OK);
    while (e.calls == 0)
        uv_run(&g_loop, UV_RUN_ONCE);
    CHECK(e.stat == ONCRPC_OK && sum == 3);
    oncRPCClientClose(c, NULL, NULL);
    oncRPCServerClose(s, NULL, NULL);
    run();
}

/* ---- 4. Out-of-memory sweep -------------------------------------------------------- */

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
        xdr_free_calc_result(&A, (calc_result *)res);
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
            oc[i].outstanding += calc_compute_1(c, &a, &oc[i].res, o_compute, NULL) == ONCRPC_OK;
            oc[i].outstanding += calc_echo_1(c, "x", &oc[i].echo, o_echo, NULL) == ONCRPC_OK;
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
    errors(port, deadport);
    oncRPCServerClose(s, NULL, NULL);
    run();
    CHECK(g_live == 0);

    printf("  lifecycle\n");
    lifecycle(port);
    CHECK(g_live == 0);

    oom_sweep(0);
    oom_sweep(1);

    CHECK(uv_loop_close(&g_loop) == 0);
    CHECK(g_live == 0);
    printf("test_rpc: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
