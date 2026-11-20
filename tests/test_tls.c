/*
 * test_tls.c - RPC-with-TLS (RFC 9289): a client and a server in one loop.
 *
 *  - the AUTH_TLS request and the server's "STARTTLS" answer, byte by byte
 *  - calls over TLS: one, many at once, a large argument and result
 *  - certificate checks: trusted or not, server name and IP address,
 *    checking switched off
 *  - mutual TLS: a client certificate required, given or missing, and the
 *    subject the server sees
 *  - a server without TLS; a server that requires it (TCP, UDP, local)
 *  - a connection closed when idle and remade, with a new TLS session
 *  - configuration errors
 *
 * The certificates are in tls_certs.h (made by tests/make_test_certs.sh).
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
#include "tls_certs.h"

static int g_fail, g_count;

#define CHECK(cond) do { g_count++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

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

/* ---- The service ------------------------------------------------------------------ */

/* What CALC_ADD saw of its last caller. */
static int  g_addTls;
static int  g_addHasPeer;
static char g_addPeer[256];

void oncRPCCalcAddV1Svc(const int32_t *a, const int32_t *b, oncRPCSvcReq *req)
{
    int32_t sum = *a + *b;
    g_addTls = oncRPCSvcTls(req);
    g_addHasPeer = oncRPCSvcTlsPeer(req, g_addPeer, sizeof(g_addPeer)) == 0;
    (void)oncRPCCalcAddV1Reply(req, &sum);
}

void oncRPCCalcEchoV1Svc(const char *s, oncRPCSvcReq *req)
{
    (void)oncRPCCalcEchoV1Reply(req, s);
}

void oncRPCCalcNullV1Svc(oncRPCSvcReq *req) { (void)oncRPCCalcNullV1Reply(req); }
void oncRPCCalcComputeV1Svc(const calc_args *a, oncRPCSvcReq *req) { (void)a; oncRPCSvcErrNoProc(req); }
void oncRPCCalcStatsV1Svc(const value_list *a, oncRPCSvcReq *req) { (void)a; oncRPCSvcErrNoProc(req); }
void oncRPCCalcReverseV1Svc(const name_list *a, oncRPCSvcReq *req) { (void)a; oncRPCSvcErrNoProc(req); }

/* ---- Helpers ------------------------------------------------------------------------ */

static oncRPCTlsConfig *server_config(const char *cert, const char *key, oncRPCTlsVerify verify)
{
    oncRPCTlsConfig *cfg = NULL;
    CHECK(oncRPCTlsConfigCreate(&A, ONCRPC_TLS_SERVER, &cfg) == ONCRPC_OK);
    CHECK(oncRPCTlsConfigSetCertificate(cfg, cert, key) == ONCRPC_OK);
    if (verify != ONCRPC_TLS_VERIFY_NONE) {
        CHECK(oncRPCTlsConfigAddTrusted(cfg, TEST_CA_CERT) == ONCRPC_OK);
        CHECK(oncRPCTlsConfigSetVerify(cfg, verify) == ONCRPC_OK);
    }
    return cfg;
}

/* trusted NULL: do not check the server at all. */
static oncRPCTlsConfig *client_config(const char *trusted, const char *cert, const char *key)
{
    oncRPCTlsConfig *cfg = NULL;
    CHECK(oncRPCTlsConfigCreate(&A, ONCRPC_TLS_CLIENT, &cfg) == ONCRPC_OK);
    if (trusted != NULL)
        CHECK(oncRPCTlsConfigAddTrusted(cfg, trusted) == ONCRPC_OK);
    else
        CHECK(oncRPCTlsConfigSetVerify(cfg, ONCRPC_TLS_VERIFY_NONE) == ONCRPC_OK);
    if (cert != NULL)
        CHECK(oncRPCTlsConfigSetCertificate(cfg, cert, key) == ONCRPC_OK);
    return cfg;
}

static char g_localPath[256];

/* A server for CALC on TCP and UDP (127.0.0.1, any port), offering TLS with
 * cfg (unless NULL); with required set, also on a local socket. */
static oncRPCServer *start_server(oncRPCTlsConfig *cfg, int required)
{
    oncRPCServer *s = NULL;
    CHECK(oncRPCServerCreate(&g_loop, &A, &s) == ONCRPC_OK);
    CHECK(oncRPCServerListen(s, ONCRPC_PROTO_TCP, "127.0.0.1", 0) == ONCRPC_OK);
    CHECK(oncRPCServerListen(s, ONCRPC_PROTO_UDP, "127.0.0.1", 0) == ONCRPC_OK);
    if (required)
        CHECK(oncRPCServerListen(s, ONCRPC_PROTO_LOCAL, g_localPath, 0) == ONCRPC_OK);
    CHECK(oncRPCCalcProgV1Register(s, NULL) == ONCRPC_OK);
    if (cfg != NULL)
        CHECK(oncRPCServerSetTls(s, cfg, required) == ONCRPC_OK);
    return s;
}

static int g_closed;

static void on_closed(void *data)
{
    (void)data;
    g_closed++;
}

static void stop_server(oncRPCServer *s)
{
    g_closed = 0;
    oncRPCServerClose(s, on_closed, NULL);
    WAIT(g_closed == 1);
}

typedef struct result {
    int        done;
    oncRPCStat st;
    int32_t    sum;
    oncRPCAuthStat why;
    char       detail[192];
} result;

static void on_add(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    result *r = (result *)data;
    const oncRPCError *e = oncRPCClientLastError(c);
    (void)res;
    r->st = st;
    r->why = e->why;
    snprintf(r->detail, sizeof(r->detail), "%s", e->detail != NULL ? e->detail : "");
    r->done = 1;
}

static oncRPCClient *client(const char *host, uint16_t port, oncRPCProto proto,
                            oncRPCTlsConfig *cfg, const char *serverName)
{
    oncRPCClient *c = NULL;
    CHECK(oncRPCClientCreate(&g_loop, &A, host, port, CALC_PROG, CALC_VERS, proto, NULL, NULL,
                             &c) == ONCRPC_OK);
    oncRPCClientSetTimeout(c, 10000);
    if (cfg != NULL)
        CHECK(oncRPCClientSetTls(c, cfg, serverName) == ONCRPC_OK);
    return c;
}

static void close_client(oncRPCClient *c)
{
    g_closed = 0;
    oncRPCClientClose(c, on_closed, NULL);
    WAIT(g_closed == 1);
}

/* One ADD(20, 22) on a new client; the client is closed afterwards. */
static result add_once(const char *host, uint16_t port, oncRPCProto proto, oncRPCTlsConfig *cfg,
                       const char *serverName)
{
    result r;
    int32_t a = 20, b = 22;
    oncRPCClient *c = client(host, port, proto, cfg, serverName);
    memset(&r, 0, sizeof(r));
    CHECK(oncRPCCalcAddV1(c, &a, &b, &r.sum, on_add, &r) == ONCRPC_OK);
    WAIT(r.done);
    if (r.st == ONCRPC_OK)
        CHECK(r.sum == 42);
    close_client(c);
    return r;
}

static int g_slept;

static void on_sleep(uv_timer_t *t)
{
    (void)t;
    g_slept = 1;
}

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

static uint64_t stat_tls(oncRPCServer *s, int failures)
{
    oncRPCServerStats st;
    oncRPCServerGetStats(s, &st);
    return failures ? st.tlsFailures : st.tlsSessions;
}

/* ---- 1. The AUTH_TLS request on the wire ---------------------------------------------- */

typedef struct raw {
    uv_tcp_t     tcp;
    uv_connect_t conn;
    uv_write_t   wr;
    uint8_t      in[256];
    size_t       len;
    int          connected, closed;
} raw;

static void raw_alloc(uv_handle_t *h, size_t s, uv_buf_t *b)
{
    raw *r = (raw *)h->data;
    (void)s;
    *b = uv_buf_init((char *)r->in + r->len, (unsigned int)(sizeof(r->in) - r->len));
}

static void raw_read(uv_stream_t *st, ssize_t n, const uv_buf_t *b)
{
    raw *r = (raw *)st->data;
    (void)b;
    if (n > 0)
        r->len += (size_t)n;
}

static void raw_connected(uv_connect_t *req, int status)
{
    raw *r = (raw *)req->data;
    r->connected = status == 0 ? 1 : -1;
}

static void raw_closed(uv_handle_t *h)
{
    ((raw *)h->data)->closed = 1;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void test_wire(void)
{
    static const uint8_t expect[] = {
        0x80, 0, 0, 32,             /* record mark: last fragment, 32 bytes */
        0x12, 0x34, 0x56, 0x78,     /* xid */
        0, 0, 0, 1,                 /* REPLY */
        0, 0, 0, 0,                 /* MSG_ACCEPTED */
        0, 0, 0, 0,                 /* verifier: AUTH_NONE */
        0, 0, 0, 8, 'S', 'T', 'A', 'R', 'T', 'T', 'L', 'S',
        0, 0, 0, 0                  /* SUCCESS, no results */
    };
    static const uint32_t call[] = {
        0x80000000u | 40, 0x12345678, 0, 2, CALC_PROG, CALC_VERS, 0, 7, 0, 0, 0
    };
    oncRPCTlsConfig *cfg = server_config(TEST_SERVER_CERT, TEST_SERVER_KEY,
                                         ONCRPC_TLS_VERIFY_NONE);
    oncRPCServer *s = start_server(cfg, 0);
    struct sockaddr_in to;
    uint8_t msg[44];
    uv_buf_t b;
    raw r;
    size_t i;

    printf("  the AUTH_TLS request and the STARTTLS answer\n");
    for (i = 0; i < 11; i++)
        put32(msg + 4 * i, call[i]);
    memset(&r, 0, sizeof(r));
    uv_tcp_init(&g_loop, &r.tcp);
    r.tcp.data = &r;
    r.conn.data = &r;
    uv_ip4_addr("127.0.0.1", oncRPCServerPort(s, ONCRPC_PROTO_TCP), &to);
    CHECK(uv_tcp_connect(&r.conn, &r.tcp, (const struct sockaddr *)&to, raw_connected) == 0);
    WAIT(r.connected != 0);
    CHECK(r.connected == 1);
    uv_read_start((uv_stream_t *)&r.tcp, raw_alloc, raw_read);
    b = uv_buf_init((char *)msg, sizeof(msg));
    CHECK(uv_write(&r.wr, (uv_stream_t *)&r.tcp, &b, 1, NULL) == 0);
    WAIT(r.len >= sizeof(expect));
    CHECK(r.len == sizeof(expect) && memcmp(r.in, expect, sizeof(expect)) == 0);
    uv_close((uv_handle_t *)&r.tcp, raw_closed);
    WAIT(r.closed);
    stop_server(s);
    oncRPCTlsConfigFree(cfg);
}

/* ---- 2. Calls over TLS -------------------------------------------------------------- */

typedef struct many {
    int     done, ok;
    int32_t sums[100];
} many;

static void on_many(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    many *m = (many *)data;
    (void)c;
    (void)res;
    m->done++;
    m->ok += st == ONCRPC_OK;
}

typedef struct echo {
    int        done;
    oncRPCStat st;
    char      *out;
} echo;

static void on_echo(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    echo *e = (echo *)data;
    (void)c;
    (void)res;
    e->st = st;
    e->done = 1;
}

static void test_calls(void)
{
    oncRPCTlsConfig *scfg = server_config(TEST_SERVER_CERT, TEST_SERVER_KEY,
                                          ONCRPC_TLS_VERIFY_NONE);
    oncRPCTlsConfig *ccfg = client_config(TEST_CA_CERT, NULL, NULL);
    oncRPCServer *s = start_server(scfg, 0);
    uint16_t port = oncRPCServerPort(s, ONCRPC_PROTO_TCP);
    oncRPCClient *c;
    result r;
    many m;
    echo e;
    char *big;
    int32_t a[100], b = 1000;
    int i;
    const size_t bigLen = 300000;

    printf("  calls over TLS\n");
    /* The configurations may be released at once: users keep them alive. */
    oncRPCTlsConfigFree(scfg);

    r = add_once("localhost", port, ONCRPC_PROTO_TCP, ccfg, NULL);
    CHECK(r.st == ONCRPC_OK);
    CHECK(g_addTls == 1 && !g_addHasPeer);
    CHECK(stat_tls(s, 0) == 1 && stat_tls(s, 1) == 0);

    /* The certificate names 127.0.0.1 too. */
    r = add_once("127.0.0.1", port, ONCRPC_PROTO_TCP, ccfg, NULL);
    CHECK(r.st == ONCRPC_OK);

    /* Many at once, then a large string there and back. */
    c = client("localhost", port, ONCRPC_PROTO_TCP, ccfg, NULL);
    memset(&m, 0, sizeof(m));
    for (i = 0; i < 100; i++) {
        a[i] = i;
        CHECK(oncRPCCalcAddV1(c, &a[i], &b, &m.sums[i], on_many, &m) == ONCRPC_OK);
    }
    WAIT(m.done == 100);
    CHECK(m.ok == 100);
    for (i = 0; i < 100; i++)
        CHECK(m.sums[i] == i + 1000);
    big = (char *)malloc(bigLen + 1);
    for (i = 0; i < (int)bigLen; i++)
        big[i] = (char)('a' + i % 26);
    big[bigLen] = '\0';
    memset(&e, 0, sizeof(e));
    CHECK(oncRPCCalcEchoV1(c, big, &e.out, on_echo, &e) == ONCRPC_OK);
    WAIT(e.done);
    CHECK(e.st == ONCRPC_OK && e.out != NULL && strcmp(e.out, big) == 0);
    oncRPCFree(&A, e.out);
    free(big);
    close_client(c);

    /* A connection closed for being idle is remade, with a new session. */
    {
        oncRPCServerLimits lim;
        int32_t x = 1, y = 2;
        oncRPCServerGetLimits(s, &lim);
        lim.idleTimeoutMs = 30;
        CHECK(oncRPCServerSetLimits(s, &lim) == ONCRPC_OK);
        c = client("localhost", port, ONCRPC_PROTO_TCP, ccfg, NULL);
        memset(&r, 0, sizeof(r));
        CHECK(oncRPCCalcAddV1(c, &x, &y, &r.sum, on_add, &r) == ONCRPC_OK);
        WAIT(r.done);
        CHECK(r.st == ONCRPC_OK && r.sum == 3);
        {
            uint64_t before = stat_tls(s, 0);
            oncRPCServerStats st;
            do {
                uv_run(&g_loop, UV_RUN_ONCE);
                oncRPCServerGetStats(s, &st);
            } while (st.connections > 0);
            sleep_ms(50);   /* let the client see the connection go */
            memset(&r, 0, sizeof(r));
            CHECK(oncRPCCalcAddV1(c, &x, &y, &r.sum, on_add, &r) == ONCRPC_OK);
            WAIT(r.done);
            CHECK(r.st == ONCRPC_OK && r.sum == 3);
            CHECK(stat_tls(s, 0) == before + 1);
        }
        close_client(c);
    }

    stop_server(s);
    oncRPCTlsConfigFree(ccfg);
}

/* ---- 3. Checking the server's certificate --------------------------------------------- */

static void test_server_checks(void)
{
    oncRPCTlsConfig *scfg = server_config(TEST_SERVER_CERT, TEST_SERVER_KEY,
                                          ONCRPC_TLS_VERIFY_NONE);
    oncRPCTlsConfig *trusting = client_config(TEST_CA_CERT, NULL, NULL);
    oncRPCTlsConfig *wrongCa = client_config(TEST_OTHER_CA_CERT, NULL, NULL);
    oncRPCTlsConfig *careless = client_config(NULL, NULL, NULL);
    oncRPCTlsConfig *strangerCfg = server_config(TEST_STRANGER_CERT, TEST_STRANGER_KEY,
                                                 ONCRPC_TLS_VERIFY_NONE);
    oncRPCServer *s = start_server(scfg, 0);
    oncRPCServer *t = start_server(NULL, 0);
    uint16_t port = oncRPCServerPort(s, ONCRPC_PROTO_TCP);
    result r;

    printf("  checking the server's certificate\n");
    /* Not the name in the certificate. */
    r = add_once("localhost", port, ONCRPC_PROTO_TCP, trusting, "other.example");
    CHECK(r.st == ONCRPC_ERR_TLS && strstr(r.detail, "certificate") != NULL);
    /* Not an authority we trust. */
    r = add_once("localhost", port, ONCRPC_PROTO_TCP, wrongCa, NULL);
    CHECK(r.st == ONCRPC_ERR_TLS && strstr(r.detail, "certificate") != NULL);
    CHECK(stat_tls(s, 1) >= 2);
    /* Checking switched off: anything goes. */
    stop_server(s);
    s = start_server(strangerCfg, 0);
    port = oncRPCServerPort(s, ONCRPC_PROTO_TCP);
    r = add_once("localhost", port, ONCRPC_PROTO_TCP, careless, NULL);
    CHECK(r.st == ONCRPC_OK);
    r = add_once("localhost", port, ONCRPC_PROTO_TCP, trusting, NULL);
    CHECK(r.st == ONCRPC_ERR_TLS);

    /* A server without TLS refuses the request. */
    r = add_once("localhost", oncRPCServerPort(t, ONCRPC_PROTO_TCP), ONCRPC_PROTO_TCP, trusting,
                 NULL);
    CHECK(r.st == ONCRPC_ERR_TLS && strstr(r.detail, "refused TLS") != NULL);

    stop_server(s);
    stop_server(t);
    oncRPCTlsConfigFree(scfg);
    oncRPCTlsConfigFree(trusting);
    oncRPCTlsConfigFree(wrongCa);
    oncRPCTlsConfigFree(careless);
    oncRPCTlsConfigFree(strangerCfg);
}

/* ---- 4. Mutual TLS ------------------------------------------------------------------- */

static void test_mutual(void)
{
    oncRPCTlsConfig *scfg = server_config(TEST_SERVER_CERT, TEST_SERVER_KEY,
                                          ONCRPC_TLS_VERIFY_REQUIRE);
    oncRPCTlsConfig *optional = server_config(TEST_SERVER_CERT, TEST_SERVER_KEY,
                                              ONCRPC_TLS_VERIFY_PEER);
    oncRPCTlsConfig *withCert = client_config(TEST_CA_CERT, TEST_CLIENT_CERT, TEST_CLIENT_KEY);
    oncRPCTlsConfig *without = client_config(TEST_CA_CERT, NULL, NULL);
    oncRPCTlsConfig *wrongCert = client_config(TEST_CA_CERT, TEST_STRANGER_CERT,
                                               TEST_STRANGER_KEY);
    oncRPCServer *s = start_server(scfg, 0);
    uint16_t port = oncRPCServerPort(s, ONCRPC_PROTO_TCP);
    result r;

    printf("  mutual TLS\n");
    r = add_once("localhost", port, ONCRPC_PROTO_TCP, withCert, NULL);
    CHECK(r.st == ONCRPC_OK);
    CHECK(g_addTls && g_addHasPeer && strcmp(g_addPeer, "CN=test client,O=liboncrpc tests") == 0);
    /* No certificate, or one from an authority the server does not trust. */
    r = add_once("localhost", port, ONCRPC_PROTO_TCP, without, NULL);
    CHECK(r.st == ONCRPC_ERR_TLS && r.detail[0] != '\0');
    r = add_once("localhost", port, ONCRPC_PROTO_TCP, wrongCert, NULL);
    CHECK(r.st == ONCRPC_ERR_TLS && r.detail[0] != '\0');
    CHECK(stat_tls(s, 1) == 2 && stat_tls(s, 0) == 1);
    stop_server(s);

    /* Asked for but not required. */
    s = start_server(optional, 0);
    port = oncRPCServerPort(s, ONCRPC_PROTO_TCP);
    r = add_once("localhost", port, ONCRPC_PROTO_TCP, without, NULL);
    CHECK(r.st == ONCRPC_OK && g_addTls && !g_addHasPeer);
    r = add_once("localhost", port, ONCRPC_PROTO_TCP, withCert, NULL);
    CHECK(r.st == ONCRPC_OK && g_addHasPeer);
    stop_server(s);

    oncRPCTlsConfigFree(scfg);
    oncRPCTlsConfigFree(optional);
    oncRPCTlsConfigFree(withCert);
    oncRPCTlsConfigFree(without);
    oncRPCTlsConfigFree(wrongCert);
}

/* ---- 5. A server that requires TLS ------------------------------------------------------ */

static void test_required(void)
{
    oncRPCTlsConfig *scfg = server_config(TEST_SERVER_CERT, TEST_SERVER_KEY,
                                          ONCRPC_TLS_VERIFY_NONE);
    oncRPCTlsConfig *ccfg = client_config(TEST_CA_CERT, NULL, NULL);
    oncRPCServer *s = start_server(scfg, 1);
    result r;

    printf("  a server that requires TLS\n");
    r = add_once("localhost", oncRPCServerPort(s, ONCRPC_PROTO_TCP), ONCRPC_PROTO_TCP, NULL, NULL);
    CHECK(r.st == ONCRPC_ERR_AUTHERROR && r.why == ONCRPC_AUTH_TOOWEAK);
    r = add_once("localhost", oncRPCServerPort(s, ONCRPC_PROTO_UDP), ONCRPC_PROTO_UDP, NULL, NULL);
    CHECK(r.st == ONCRPC_ERR_AUTHERROR && r.why == ONCRPC_AUTH_TOOWEAK);
    r = add_once(g_localPath, 0, ONCRPC_PROTO_LOCAL, NULL, NULL);
    CHECK(r.st == ONCRPC_OK && !g_addTls);
    r = add_once("localhost", oncRPCServerPort(s, ONCRPC_PROTO_TCP), ONCRPC_PROTO_TCP, ccfg, NULL);
    CHECK(r.st == ONCRPC_OK && g_addTls);
    stop_server(s);
    oncRPCTlsConfigFree(scfg);
    oncRPCTlsConfigFree(ccfg);
}

/* ---- 6. Configuration errors ---------------------------------------------------------- */

static void test_config_errors(void)
{
    oncRPCTlsConfig *cfg = NULL, *ccfg = NULL;
    oncRPCServer *s = NULL;
    oncRPCClient *c;

    printf("  configuration errors\n");
    CHECK(oncRPCTlsConfigCreate(NULL, ONCRPC_TLS_SERVER, &cfg) == ONCRPC_ERR_INVALID);
    CHECK(oncRPCTlsConfigCreate(&A, ONCRPC_TLS_SERVER, &cfg) == ONCRPC_OK);
    CHECK(oncRPCTlsConfigSetCertificate(cfg, "not a certificate", TEST_SERVER_KEY) ==
          ONCRPC_ERR_TLS);
    /* A key that does not belong to the certificate. */
    CHECK(oncRPCTlsConfigSetCertificate(cfg, TEST_SERVER_CERT, TEST_CLIENT_KEY) ==
          ONCRPC_ERR_TLS);
    CHECK(oncRPCTlsConfigAddTrusted(cfg, "nothing here") == ONCRPC_ERR_TLS);
    CHECK(oncRPCTlsConfigSetVerify(cfg, (oncRPCTlsVerify)42) == ONCRPC_ERR_INVALID);
    CHECK(oncRPCServerCreate(&g_loop, &A, &s) == ONCRPC_OK);
    /* No certificate yet. */
    CHECK(oncRPCServerSetTls(s, cfg, 0) == ONCRPC_ERR_INVALID);
    CHECK(oncRPCTlsConfigSetCertificate(cfg, TEST_SERVER_CERT, TEST_SERVER_KEY) == ONCRPC_OK);
    CHECK(oncRPCServerSetTls(s, cfg, 0) == ONCRPC_OK);
    /* Roles. */
    CHECK(oncRPCTlsConfigCreate(&A, ONCRPC_TLS_CLIENT, &ccfg) == ONCRPC_OK);
    CHECK(oncRPCServerSetTls(s, ccfg, 0) == ONCRPC_ERR_INVALID);
    c = client("localhost", 1, ONCRPC_PROTO_TCP, NULL, NULL);
    CHECK(oncRPCClientSetTls(c, cfg, NULL) == ONCRPC_ERR_INVALID);
    close_client(c);
    /* TLS is for TCP only. */
    c = client("localhost", 1, ONCRPC_PROTO_UDP, NULL, NULL);
    CHECK(oncRPCClientSetTls(c, ccfg, NULL) == ONCRPC_ERR_INVALID);
    close_client(c);
    stop_server(s);
    oncRPCTlsConfigFree(cfg);
    oncRPCTlsConfigFree(ccfg);
}

int main(void)
{
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    CHECK(uv_loop_init(&g_loop) == 0);
#ifdef _WIN32
    snprintf(g_localPath, sizeof(g_localPath), "\\\\.\\pipe\\oncrpc-tls-%d",
             (int)uv_os_getpid());
#else
    {
        char dir[200];
        size_t dlen = sizeof(dir);
        if (uv_os_tmpdir(dir, &dlen) != 0)
            snprintf(dir, sizeof(dir), "/tmp");
        snprintf(g_localPath, sizeof(g_localPath), "%s/oncrpc-tls-%d.sock", dir,
                 (int)uv_os_getpid());
    }
#endif

    test_wire();
    CHECK(g_live == 0);
    test_calls();
    CHECK(g_live == 0);
    test_server_checks();
    CHECK(g_live == 0);
    test_mutual();
    CHECK(g_live == 0);
    test_required();
    CHECK(g_live == 0);
    test_config_errors();

    uv_run(&g_loop, UV_RUN_DEFAULT);
    CHECK(uv_loop_close(&g_loop) == 0);
    CHECK(g_live == 0);
    printf("test_tls: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
