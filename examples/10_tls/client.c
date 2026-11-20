/*
 * 10_tls/client.c - calls HELLO over TLS, presenting a client certificate.
 *
 * usage: client [host] [port] [certificate directory]
 *        (defaults 127.0.0.1, 5010 and examples/10_tls/certs)
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

static char *greeting;
static int   failed;

static void on_hello(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    if (status == ONCRPC_OK) {
        printf("client: %s\n", greeting);
        oncRPCFree(&app_alloc, greeting);
    } else {
        /* For ONCRPC_ERR_TLS, detail says what went wrong - an untrusted
         * certificate, the wrong name, a server without TLS, ... */
        const oncRPCError *e = oncRPCClientLastError(client);
        printf("client: HELLO failed: %s%s%s\n", oncRPCStrError(status),
               e->detail != NULL ? ": " : "", e->detail != NULL ? e->detail : "");
        failed = 1;
    }
    oncRPCClientClose(client, NULL, NULL);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 5010;
    const char *dir = argc > 3 ? argv[3] : "examples/10_tls/certs";
    char *ca = read_pem(dir, "ca.pem");
    char *cert = read_pem(dir, "client.pem");
    char *key = read_pem(dir, "client.key");
    oncRPCTlsConfig *tls;
    oncRPCClient *client;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (ca == NULL || cert == NULL || key == NULL)
        return 1;

    /* The client's side: trust the server's certificate only if ca.pem
     * signed it (the default check also wants the certificate to name the
     * host we connect to), and present our own certificate when asked. */
    if (oncRPCTlsConfigCreate(&app_alloc, ONCRPC_TLS_CLIENT, &tls) != ONCRPC_OK ||
        oncRPCTlsConfigAddTrusted(tls, ca) != ONCRPC_OK ||
        oncRPCTlsConfigSetCertificate(tls, cert, key) != ONCRPC_OK) {
        fprintf(stderr, "client: cannot set up TLS with the files in %s\n", dir);
        return 1;
    }
    free(ca);
    free(cert);
    free(key);

    if (oncRPCClientCreate(loop, &app_alloc, host, (uint16_t)port, HELLO_PROG, HELLO_VERS,
                           ONCRPC_PROTO_TCP, NULL, NULL, &client) != ONCRPC_OK)
        return 1;
    /* Upgrade the connection to TLS as soon as it is made. NULL: the
     * server's certificate must name the host given above. */
    oncRPCClientSetTls(client, tls, NULL);
    oncRPCTlsConfigFree(tls);   /* the client keeps what it needs */

    oncRPCHelloV1(client, "example client", &greeting, on_hello, NULL);
    uv_run(loop, UV_RUN_DEFAULT);
    return failed;
}
