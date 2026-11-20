/*
 * 10_tls/server.c - serves HELLO, over TLS only.
 *
 * usage: server [port] [certificate directory]
 *        (defaults 5010 and examples/10_tls/certs)
 *
 * Clients must upgrade their connection to TLS (RFC 9289) before calling.
 * A client may also present a certificate signed by ca.pem; the server
 * then knows who it is.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "hello.h"
#include "pem.h"

/* liboncrpc never calls malloc itself; it asks this allocator. */
static void *app_realloc(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    return realloc(ptr, size);
}

static void app_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static const oncRPCAllocator app_alloc = { app_realloc, app_free, NULL };

static oncRPCServer *server;
static uv_signal_t   sigint, sigterm;

void oncRPCHelloV1Svc(const char *name, oncRPCSvcReq *req)
{
    char who[256], answer[512];

    /* The subject of the client's certificate, if it presented one that
     * checks out against ca.pem. */
    if (oncRPCSvcTlsPeer(req, who, sizeof(who)) != 0)
        snprintf(who, sizeof(who), "nobody in particular (no client certificate)");
    snprintf(answer, sizeof(answer), "Hello, %s. Your certificate says you are %s.", name, who);
    printf("server: HELLO from %s\n", who);
    fflush(stdout);
    oncRPCHelloV1Reply(req, answer);
}

static void on_signal(uv_signal_t *h, int signum)
{
    (void)h;
    (void)signum;
    uv_close((uv_handle_t *)&sigint, NULL);
    uv_close((uv_handle_t *)&sigterm, NULL);
    oncRPCServerClose(server, NULL, NULL);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    int port = argc > 1 ? atoi(argv[1]) : 5010;
    const char *dir = argc > 2 ? argv[2] : "examples/10_tls/certs";
    char *cert = read_pem(dir, "server.pem");
    char *key = read_pem(dir, "server.key");
    char *ca = read_pem(dir, "ca.pem");
    oncRPCTlsConfig *tls;
    oncRPCStat status;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (cert == NULL || key == NULL || ca == NULL)
        return 1;

    /* The server's side of TLS: its certificate and key, and the authority
     * whose client certificates it accepts. VERIFY_PEER asks clients for a
     * certificate but lets them in without one; VERIFY_REQUIRE would turn
     * those away. */
    if (oncRPCTlsConfigCreate(&app_alloc, ONCRPC_TLS_SERVER, &tls) != ONCRPC_OK ||
        oncRPCTlsConfigSetCertificate(tls, cert, key) != ONCRPC_OK ||
        oncRPCTlsConfigAddTrusted(tls, ca) != ONCRPC_OK ||
        oncRPCTlsConfigSetVerify(tls, ONCRPC_TLS_VERIFY_PEER) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot set up TLS with the files in %s\n", dir);
        return 1;
    }
    free(cert);
    free(key);
    free(ca);

    if (oncRPCServerCreate(loop, &app_alloc, &server) != ONCRPC_OK)
        return 1;
    status = oncRPCServerListen(server, ONCRPC_PROTO_TCP, "127.0.0.1", (uint16_t)port);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "server: cannot listen on port %d: %s\n", port, oncRPCStrError(status));
        return 1;
    }
    /* Offer TLS, and refuse calls that do not use it (required = 1). */
    oncRPCServerSetTls(server, tls, 1);
    oncRPCTlsConfigFree(tls);   /* the server keeps what it needs */
    oncRPCHelloProgV1Register(server, NULL);
    printf("server: listening on 127.0.0.1:%d, TLS required\n", port);
    fflush(stdout);

    uv_signal_init(loop, &sigint);
    uv_signal_start(&sigint, on_signal, SIGINT);
    uv_signal_init(loop, &sigterm);
    uv_signal_start(&sigterm, on_signal, SIGTERM);
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
