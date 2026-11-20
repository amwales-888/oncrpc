/*
 * 09_local/server.c - serves WHO_AM_I on a local socket.
 *
 * usage: server [path]   (default /tmp/oncrpc-09.sock; on Windows the pipe
 *                         \\.\pipe\oncrpc-09)
 *
 * A local socket is a file in the file system (a name in the pipe namespace
 * on Windows): only programs on this machine can connect, and the socket's
 * permissions decide which ones. The operating system also tells the server
 * which user is calling, which WHO_AM_I sends back.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "local.h"

#ifdef _WIN32
#define DEFAULT_PATH "\\\\.\\pipe\\oncrpc-09"
#else
#define DEFAULT_PATH "/tmp/oncrpc-09.sock"
#endif

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

void oncRPCWhoAmIV1Svc(oncRPCSvcReq *req)
{
    char answer[96];
    uint32_t uid, gid;

    /* Known on Linux, macOS and the BSDs; Windows does not say. */
    if (oncRPCSvcLocalCred(req, &uid, &gid) == 0)
        snprintf(answer, sizeof(answer), "uid %lu, gid %lu", (unsigned long)uid,
                 (unsigned long)gid);
    else
        snprintf(answer, sizeof(answer), "a local caller (the system does not say who)");
    printf("server: WHO_AM_I from %s\n", answer);
    fflush(stdout);
    oncRPCWhoAmIV1Reply(req, answer);
}

static void on_signal(uv_signal_t *h, int signum)
{
    (void)h;
    (void)signum;
    printf("server: shutting down\n");
    fflush(stdout);
    uv_close((uv_handle_t *)&sigint, NULL);
    uv_close((uv_handle_t *)&sigterm, NULL);
    /* Closing the server also removes the socket file. */
    oncRPCServerClose(server, NULL, NULL);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    const char *path = DEFAULT_PATH;
    oncRPCStat status;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (argc > 1)
        path = argv[1];

    if (oncRPCServerCreate(loop, &app_alloc, &server) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot create server\n");
        return 1;
    }
    /* For ONCRPC_PROTO_LOCAL the address is a path and there is no port. */
    status = oncRPCServerListen(server, ONCRPC_PROTO_LOCAL, path, 0);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "server: cannot listen on %s: %s\n", path, oncRPCStrError(status));
        fprintf(stderr, "server: if no server is running there, delete the file and retry\n");
        oncRPCServerClose(server, NULL, NULL);
        uv_run(loop, UV_RUN_DEFAULT);
        return 1;
    }
    oncRPCLocalProgV1Register(server, NULL);
    printf("server: listening on %s\n", path);
    fflush(stdout);

    /* Stop cleanly on Ctrl-C or kill, so that the socket file goes away. */
    uv_signal_init(loop, &sigint);
    uv_signal_start(&sigint, on_signal, SIGINT);
    uv_signal_init(loop, &sigterm);
    uv_signal_start(&sigterm, on_signal, SIGTERM);

    uv_run(loop, UV_RUN_DEFAULT);
    printf("server: closed\n");
    return 0;
}
