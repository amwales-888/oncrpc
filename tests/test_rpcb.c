/*
 * test_rpcb.c - rpcbind (versions 3 and 4) and port mapper (version 2).
 *
 *  1. universal addresses: formatting and parsing, IPv4 and IPv6, and
 *     malformed input (always runs)
 *  2. with an rpcbind daemon on 127.0.0.1:111: set, look up and unset
 *     entries by network id; a server registers its listeners and clients
 *     with port 0 find it over TCP and UDP; a local (Unix socket) listener
 *     is registered as netid "local" and found by its path; unregistering
 *     removes it all
 *  3. with nothing on port 111 and permission to listen there: a stand-in
 *     port mapper that speaks only version 2, to check that registration
 *     and look-ups fall back to it, and that the version 3/4 functions
 *     report the failure
 *
 * Parts 2 and 3 are skipped (with a message) when they cannot run.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oncrpc.h"

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

/* The test service: procedure 1 answers 42. */
#define TEST_PROG 0x20000321u
#define TEST_VERS 1u

/* ---- 1. Universal addresses -------------------------------------------------------- */

static void uaddr_case(const char *ip, int port, const char *expect)
{
    struct sockaddr_storage a, b;
    char buf[ONCRPC_UADDR_MAX];

    memset(&a, 0, sizeof(a));
    if (strchr(ip, ':') == NULL)
        CHECK(uv_ip4_addr(ip, port, (struct sockaddr_in *)&a) == 0);
    else
        CHECK(uv_ip6_addr(ip, port, (struct sockaddr_in6 *)&a) == 0);
    CHECK(oncRPCUaddrFromSockaddr((const struct sockaddr *)&a, buf, sizeof(buf)) == 0);
    if (strcmp(buf, expect) != 0) {
        g_fail++;
        fprintf(stderr, "%s port %d: got '%s', expected '%s'\n", ip, port, buf, expect);
    }
    CHECK(oncRPCUaddrToSockaddr(expect, &b) == 0);
    CHECK(b.ss_family == a.ss_family);
    if (a.ss_family == AF_INET)
        CHECK(memcmp(&((struct sockaddr_in *)&a)->sin_addr, &((struct sockaddr_in *)&b)->sin_addr,
                     4) == 0 &&
              ((struct sockaddr_in *)&a)->sin_port == ((struct sockaddr_in *)&b)->sin_port);
    else
        CHECK(memcmp(&((struct sockaddr_in6 *)&a)->sin6_addr,
                     &((struct sockaddr_in6 *)&b)->sin6_addr, 16) == 0 &&
              ((struct sockaddr_in6 *)&a)->sin6_port == ((struct sockaddr_in6 *)&b)->sin6_port);
}

static void test_uaddr(void)
{
    static const char *const bad[] = {
        "", "1.2.3.4", "1.2.3.4.5", "1.2.3.4.256.1", "1.2.3.4.1.x", "1.2.3.4..1",
        "300.1.1.1.0.1", "::1.4", "a.b.c.d.1.1", "1.2.3.4.1.1234", "::g.0.1",
        "1.2.3.4.1.-1"
    };
    struct sockaddr_storage a;
    struct sockaddr_in in4;
    char small[8];
    size_t i;

    printf("  universal addresses\n");
    uaddr_case("127.0.0.1", 1025, "127.0.0.1.4.1");
    uaddr_case("0.0.0.0", 111, "0.0.0.0.0.111");
    uaddr_case("192.0.2.7", 65535, "192.0.2.7.255.255");
    uaddr_case("10.0.0.1", 0, "10.0.0.1.0.0");
    uaddr_case("::1", 1025, "::1.4.1");
    uaddr_case("::", 111, "::.0.111");
    uaddr_case("2001:db8::7", 2049, "2001:db8::7.8.1");
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        g_count++;
        if (oncRPCUaddrToSockaddr(bad[i], &a) == 0) {
            g_fail++;
            fprintf(stderr, "'%s' was accepted as a universal address\n", bad[i]);
        }
    }
    CHECK(oncRPCUaddrToSockaddr(NULL, &a) == -1);
    uv_ip4_addr("127.0.0.1", 1025, &in4);
    CHECK(oncRPCUaddrFromSockaddr((const struct sockaddr *)&in4, small, sizeof(small)) == -1);
    CHECK(strcmp(oncRPCNetid(ONCRPC_PROTO_TCP, AF_INET), "tcp") == 0);
    CHECK(strcmp(oncRPCNetid(ONCRPC_PROTO_UDP, AF_INET), "udp") == 0);
    CHECK(strcmp(oncRPCNetid(ONCRPC_PROTO_TCP, AF_INET6), "tcp6") == 0);
    CHECK(strcmp(oncRPCNetid(ONCRPC_PROTO_UDP, AF_INET6), "udp6") == 0);
    CHECK(oncRPCNetid((oncRPCProto)99, AF_INET) == NULL);
}

/* ---- Helpers ----------------------------------------------------------------------- */

typedef struct answer {
    int        done;
    oncRPCStat st;
    uint32_t   value;
    char       uaddr[ONCRPC_UADDR_MAX];
} answer;

static void on_value(oncRPCStat st, uint32_t value, void *data)
{
    answer *a = (answer *)data;
    a->st = st;
    a->value = value;
    a->done = 1;
}

static void on_addr(oncRPCStat st, const char *uaddr, void *data)
{
    answer *a = (answer *)data;
    a->st = st;
    snprintf(a->uaddr, sizeof(a->uaddr), "%s", uaddr != NULL ? uaddr : "");
    a->done = 1;
}

static void on_srv_pmap(oncRPCServer *srv, oncRPCStat st, void *data)
{
    answer *a = (answer *)data;
    (void)srv;
    a->st = st;
    a->done = 1;
}

static int xdr_u32(oncRPCXdr *x, void *p)
{
    return oncRPCXdrUint32(x, (uint32_t *)p);
}

static void test_dispatch(oncRPCSvcReq *req)
{
    uint32_t v = 42;
    if (req->proc == 1)
        oncRPCSvcReply(req, xdr_u32, &v);
    else
        oncRPCSvcReply(req, NULL, NULL);
}

static void on_call(oncRPCClient *c, oncRPCStat st, void *res, void *data)
{
    answer *a = (answer *)data;
    (void)res;
    a->st = st;
    a->done = 1;
    oncRPCClientClose(c, NULL, NULL);
}

/* Call TEST_PROG procedure 1 with port 0, so that the client asks the
 * daemon for the port. Returns the status; the answer must be 42. */
static oncRPCStat call_via_lookup(oncRPCProto proto)
{
    oncRPCClient *c;
    answer a;
    uint32_t v = 0;

    memset(&a, 0, sizeof(a));
    if (oncRPCClientCreate(&g_loop, &A, "127.0.0.1", 0, TEST_PROG, TEST_VERS, proto, NULL, NULL,
                           &c) != ONCRPC_OK)
        return ONCRPC_ERR_FAILED;
    oncRPCClientSetTimeout(c, 8000);
    if (oncRPCClientCall(c, 1, NULL, NULL, xdr_u32, &v, on_call, &a) != ONCRPC_OK) {
        oncRPCClientClose(c, NULL, NULL);
        return ONCRPC_ERR_FAILED;
    }
    WAIT(a.done);
    if (a.st == ONCRPC_OK)
        CHECK(v == 42);
    return a.st;
}

static oncRPCServer *start_service(void)
{
    oncRPCServer *s;
    if (oncRPCServerCreate(&g_loop, &A, &s) != ONCRPC_OK)
        return NULL;
    if (oncRPCServerListen(s, ONCRPC_PROTO_TCP, "127.0.0.1", 0) != ONCRPC_OK ||
        oncRPCServerListen(s, ONCRPC_PROTO_UDP, "127.0.0.1", 0) != ONCRPC_OK ||
        oncRPCServerRegister(s, TEST_PROG, TEST_VERS, test_dispatch, NULL) != ONCRPC_OK) {
        oncRPCServerClose(s, NULL, NULL);
        return NULL;
    }
    return s;
}

static void close_service(oncRPCServer *s)
{
    oncRPCServerClose(s, NULL, NULL);
    uv_run(&g_loop, UV_RUN_DEFAULT);
}

static answer getaddr(const char *netid)
{
    answer a;
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCRpcbGetAddr(&g_loop, &A, "127.0.0.1", TEST_PROG, TEST_VERS, netid, on_addr, &a) ==
          ONCRPC_OK);
    WAIT(a.done);
    return a;
}

static answer pmap_getport(oncRPCProto proto)
{
    answer a;
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCPmapGetPort(&g_loop, &A, "127.0.0.1", TEST_PROG, TEST_VERS, proto, on_value,
                            &a) == ONCRPC_OK);
    WAIT(a.done);
    return a;
}

/* A path for a local listener (a pipe name on Windows). */
static void local_path(char *buf, size_t len)
{
#ifdef _WIN32
    snprintf(buf, len, "\\\\.\\pipe\\oncrpc-rpcb-%d", (int)uv_os_getpid());
#else
    char dir[200];
    size_t dlen = sizeof(dir);
    uv_fs_t req;
    if (uv_os_tmpdir(dir, &dlen) != 0)
        snprintf(dir, sizeof(dir), "/tmp");
    snprintf(buf, len, "%s/oncrpc-rpcb-%d.sock", dir, (int)uv_os_getpid());
    (void)uv_fs_unlink(&g_loop, &req, buf, NULL);
    uv_fs_req_cleanup(&req);
#endif
}

static oncRPCServer *start_service_local(const char *path)
{
    oncRPCServer *s = start_service();
    if (s != NULL && oncRPCServerListen(s, ONCRPC_PROTO_LOCAL, path, 0) != ONCRPC_OK) {
        oncRPCServerClose(s, NULL, NULL);
        return NULL;
    }
    return s;
}

#ifndef _WIN32   /* Windows has no rpcbind to find local services through */
static oncRPCStat call_local(const char *path)
{
    oncRPCClient *c;
    answer a;
    uint32_t v = 0;

    memset(&a, 0, sizeof(a));
    if (oncRPCClientCreate(&g_loop, &A, path, 0, TEST_PROG, TEST_VERS, ONCRPC_PROTO_LOCAL, NULL,
                           NULL, &c) != ONCRPC_OK)
        return ONCRPC_ERR_FAILED;
    if (oncRPCClientCall(c, 1, NULL, NULL, xdr_u32, &v, on_call, &a) != ONCRPC_OK) {
        oncRPCClientClose(c, NULL, NULL);
        return ONCRPC_ERR_FAILED;
    }
    WAIT(a.done);
    if (a.st == ONCRPC_OK)
        CHECK(v == 42);
    return a.st;
}
#endif

/* Remove anything an earlier, interrupted run left behind. */
static void unset_all(void)
{
    answer a;
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCRpcbUnset(&g_loop, &A, TEST_PROG, TEST_VERS, NULL, on_value, &a) == ONCRPC_OK);
    WAIT(a.done);
}

/* ---- 2. A real rpcbind ---------------------------------------------------------------- */

static void test_rpcbind(void)
{
    oncRPCServer *s;
    answer a;
    uint16_t tport, uport;
    char want[ONCRPC_UADDR_MAX], path[ONCRPC_UADDR_MAX];

    printf("  rpcbind on 127.0.0.1:111\n");
    unset_all();

    /* Entries by network id. */
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCRpcbSet(&g_loop, &A, TEST_PROG, TEST_VERS, ONCRPC_NETID_TCP, "127.0.0.1.4.1",
                        on_value, &a) == ONCRPC_OK);
    WAIT(a.done);
    CHECK(a.st == ONCRPC_OK && a.value == 1);
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCRpcbSet(&g_loop, &A, TEST_PROG, TEST_VERS, ONCRPC_NETID_UDP, "127.0.0.1.4.2",
                        on_value, &a) == ONCRPC_OK);
    WAIT(a.done);
    CHECK(a.st == ONCRPC_OK && a.value == 1);
    /* An IPv6 entry is accepted (version 2 could not express it). */
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCRpcbSet(&g_loop, &A, TEST_PROG, TEST_VERS, ONCRPC_NETID_TCP6, "::1.4.3",
                        on_value, &a) == ONCRPC_OK);
    WAIT(a.done);
    CHECK(a.st == ONCRPC_OK && a.value == 1);

    a = getaddr(ONCRPC_NETID_TCP);
    CHECK(a.st == ONCRPC_OK && strcmp(a.uaddr, "127.0.0.1.4.1") == 0);
    a = getaddr(ONCRPC_NETID_UDP);
    CHECK(a.st == ONCRPC_OK && strcmp(a.uaddr, "127.0.0.1.4.2") == 0);
    /* Version 2 sees the IPv4 entries too. */
    a = pmap_getport(ONCRPC_PROTO_TCP);
    CHECK(a.st == ONCRPC_OK && a.value == 1025);
    a = pmap_getport(ONCRPC_PROTO_UDP);
    CHECK(a.st == ONCRPC_OK && a.value == 1026);

    /* Removing one network id leaves the others. */
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCRpcbUnset(&g_loop, &A, TEST_PROG, TEST_VERS, ONCRPC_NETID_UDP, on_value, &a) ==
          ONCRPC_OK);
    WAIT(a.done);
    CHECK(a.st == ONCRPC_OK && a.value == 1);
    a = getaddr(ONCRPC_NETID_UDP);
    CHECK(a.st == ONCRPC_ERR_PROGNOTREGISTERED && a.uaddr[0] == '\0');
    a = getaddr(ONCRPC_NETID_TCP);
    CHECK(a.st == ONCRPC_OK);
    unset_all();
    a = getaddr(ONCRPC_NETID_TCP);
    CHECK(a.st == ONCRPC_ERR_PROGNOTREGISTERED);

    /* A server registers its listeners; clients with port 0 find them. */
    local_path(path, sizeof(path));
    s = start_service_local(path);
    CHECK(s != NULL);
    if (s == NULL)
        return;
    tport = oncRPCServerPort(s, ONCRPC_PROTO_TCP);
    uport = oncRPCServerPort(s, ONCRPC_PROTO_UDP);
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCServerPmapRegister(s, TEST_PROG, TEST_VERS, on_srv_pmap, &a) == ONCRPC_OK);
    WAIT(a.done);
    CHECK(a.st == ONCRPC_OK);
    a = getaddr(ONCRPC_NETID_TCP);
    snprintf(want, sizeof(want), "127.0.0.1.%u.%u", (unsigned)(tport >> 8), (unsigned)(tport & 255));
    CHECK(a.st == ONCRPC_OK && strcmp(a.uaddr, want) == 0);
    a = getaddr(ONCRPC_NETID_UDP);
    snprintf(want, sizeof(want), "127.0.0.1.%u.%u", (unsigned)(uport >> 8), (unsigned)(uport & 255));
    CHECK(a.st == ONCRPC_OK && strcmp(a.uaddr, want) == 0);
    CHECK(call_via_lookup(ONCRPC_PROTO_TCP) == ONCRPC_OK);
    CHECK(call_via_lookup(ONCRPC_PROTO_UDP) == ONCRPC_OK);
#ifndef _WIN32
    /* The local listener is registered by its path, and the path that
     * rpcbind gives back leads to the server. */
    a = getaddr(ONCRPC_NETID_LOCAL);
    CHECK(a.st == ONCRPC_OK && strcmp(a.uaddr, path) == 0);
    if (a.st == ONCRPC_OK)
        CHECK(call_local(a.uaddr) == ONCRPC_OK);
#endif

    /* Registering again replaces the entries rather than failing. */
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCServerPmapRegister(s, TEST_PROG, TEST_VERS, on_srv_pmap, &a) == ONCRPC_OK);
    WAIT(a.done);
    CHECK(a.st == ONCRPC_OK);

    memset(&a, 0, sizeof(a));
    CHECK(oncRPCServerPmapUnregister(s, TEST_PROG, TEST_VERS, on_srv_pmap, &a) == ONCRPC_OK);
    WAIT(a.done);
    CHECK(a.st == ONCRPC_OK);
    a = getaddr(ONCRPC_NETID_TCP);
    CHECK(a.st == ONCRPC_ERR_PROGNOTREGISTERED);
#ifndef _WIN32
    a = getaddr(ONCRPC_NETID_LOCAL);
    CHECK(a.st == ONCRPC_ERR_PROGNOTREGISTERED);
#endif
    CHECK(call_via_lookup(ONCRPC_PROTO_TCP) == ONCRPC_ERR_PROGNOTREGISTERED);
    close_service(s);
}

/* ---- 3. A stand-in port mapper that speaks only version 2 ------------------------------ */

typedef struct mapping {
    uint32_t prog, vers, prot, port;
} mapping;

static mapping g_map[32];
static int     g_nmap;
static int     g_v2calls;

static int xdr_mapping(oncRPCXdr *x, void *p)
{
    mapping *m = (mapping *)p;
    return oncRPCXdrUint32(x, &m->prog) && oncRPCXdrUint32(x, &m->vers) &&
           oncRPCXdrUint32(x, &m->prot) && oncRPCXdrUint32(x, &m->port);
}

static void pmap2_dispatch(oncRPCSvcReq *req)
{
    mapping *m;
    uint32_t r = 0;
    int i;

    if (req->proc == 0) {
        oncRPCSvcReply(req, NULL, NULL);
        return;
    }
    if (req->proc > 3) {
        oncRPCSvcErrNoProc(req);
        return;
    }
    m = (mapping *)oncRPCSvcDecodeArgs(req, xdr_mapping, sizeof(*m));
    if (m == NULL) {
        oncRPCSvcErrDecode(req);
        return;
    }
    g_v2calls++;
    switch (req->proc) {
    case 1:   /* SET: fails if the program version and protocol are taken */
        r = 1;
        for (i = 0; i < g_nmap; i++)
            if (g_map[i].prog == m->prog && g_map[i].vers == m->vers && g_map[i].prot == m->prot)
                r = 0;
        if (r && g_nmap < 32)
            g_map[g_nmap++] = *m;
        break;
    case 2:   /* UNSET: every protocol */
        for (i = 0; i < g_nmap;) {
            if (g_map[i].prog == m->prog && g_map[i].vers == m->vers) {
                g_map[i] = g_map[--g_nmap];
                r = 1;
            } else {
                i++;
            }
        }
        break;
    default:  /* GETPORT */
        for (i = 0; i < g_nmap; i++)
            if (g_map[i].prog == m->prog && g_map[i].vers == m->vers && g_map[i].prot == m->prot)
                r = g_map[i].port;
        break;
    }
    oncRPCSvcReply(req, xdr_u32, &r);
}

static uint32_t mapped_port(uint32_t prot)
{
    int i;
    for (i = 0; i < g_nmap; i++)
        if (g_map[i].prog == TEST_PROG && g_map[i].vers == TEST_VERS && g_map[i].prot == prot)
            return g_map[i].port;
    return 0;
}

static void test_portmap_v2(void)
{
    oncRPCServer *pm, *s;
    answer a;
    char path[ONCRPC_UADDR_MAX];

    if (oncRPCServerCreate(&g_loop, &A, &pm) != ONCRPC_OK)
        return;
    if (oncRPCServerListen(pm, ONCRPC_PROTO_UDP, "127.0.0.1", ONCRPC_PMAP_PORT) != ONCRPC_OK ||
        oncRPCServerListen(pm, ONCRPC_PROTO_TCP, "127.0.0.1", ONCRPC_PMAP_PORT) != ONCRPC_OK) {
        printf("  version 2 fallback: skipped (cannot listen on port 111)\n");
        close_service(pm);
        return;
    }
    printf("  version 2 fallback (stand-in port mapper on 127.0.0.1:111)\n");
    CHECK(oncRPCServerRegister(pm, ONCRPC_PMAP_PROG, ONCRPC_PMAP_VERS, pmap2_dispatch, NULL) ==
          ONCRPC_OK);

    local_path(path, sizeof(path));
    s = start_service_local(path);
    CHECK(s != NULL);
    if (s == NULL) {
        close_service(pm);
        return;
    }
    /* Registration: versions 4 and 3 are refused, so version 2 is used. It
     * cannot describe the local listener, which is reported, but the TCP
     * and UDP listeners are registered. */
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCServerPmapRegister(s, TEST_PROG, TEST_VERS, on_srv_pmap, &a) == ONCRPC_OK);
    WAIT(a.done);
    CHECK(a.st == ONCRPC_ERR_PMAPFAILURE);
    CHECK(mapped_port(ONCRPC_PROTO_TCP) == oncRPCServerPort(s, ONCRPC_PROTO_TCP));
    CHECK(mapped_port(ONCRPC_PROTO_UDP) == oncRPCServerPort(s, ONCRPC_PROTO_UDP));

    /* Look-ups fall back too. */
    CHECK(call_via_lookup(ONCRPC_PROTO_TCP) == ONCRPC_OK);
    CHECK(call_via_lookup(ONCRPC_PROTO_UDP) == ONCRPC_OK);

    /* The version 3/4 functions do not fall back to 2: they report it. */
    a = getaddr(ONCRPC_NETID_TCP);
    CHECK(a.st == ONCRPC_ERR_PMAPFAILURE);

    memset(&a, 0, sizeof(a));
    CHECK(oncRPCServerPmapUnregister(s, TEST_PROG, TEST_VERS, on_srv_pmap, &a) == ONCRPC_OK);
    WAIT(a.done);
    CHECK(a.st == ONCRPC_OK);
    CHECK(g_nmap == 0);
    CHECK(g_v2calls >= 6);
    oncRPCServerClose(s, NULL, NULL);
    close_service(pm);
}

/* ---- main ---------------------------------------------------------------------------- */

int main(void)
{
    answer a;

#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    CHECK(uv_loop_init(&g_loop) == 0);

    test_uaddr();

    /* Is an rpcbind (version 4) daemon running here? */
    memset(&a, 0, sizeof(a));
    CHECK(oncRPCRpcbGetAddr(&g_loop, &A, "127.0.0.1", ONCRPC_RPCB_PROG, ONCRPC_RPCB_VERS4,
                            ONCRPC_NETID_UDP, on_addr, &a) == ONCRPC_OK);
    WAIT(a.done);
    if (a.st == ONCRPC_OK) {
        CHECK(strcmp(a.uaddr, "127.0.0.1.0.111") == 0);
        test_rpcbind();
    } else {
        /* Anything (such as an old portmap) on port 111? */
        memset(&a, 0, sizeof(a));
        CHECK(oncRPCPmapGetPort(&g_loop, &A, "127.0.0.1", ONCRPC_PMAP_PROG, ONCRPC_PMAP_VERS,
                                ONCRPC_PROTO_UDP, on_value, &a) == ONCRPC_OK);
        WAIT(a.done);
        if (a.st == ONCRPC_OK)
            printf("  skipped: a port mapper without rpcbind version 4 is running\n");
        else
            test_portmap_v2();
    }
    uv_run(&g_loop, UV_RUN_DEFAULT);
    CHECK(uv_loop_close(&g_loop) == 0);
    CHECK(g_live == 0);
    printf("test_rpcb: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
