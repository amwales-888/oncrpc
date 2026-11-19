/*
 * 06_portmap/server.c - registers with the port mapper; shuts down cleanly.
 *
 * usage: server
 *
 * The server listens on TCP and UDP ports chosen by the system and tells the
 * local port mapper (rpcbind) where to find CLOCK_PROG. Clients only need
 * the program number. On Ctrl-C (SIGINT) or SIGTERM it removes its
 * registration and closes, and uv_run() returns.
 *
 * Needs rpcbind running (and, depending on its configuration, root).
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "clock.h"

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
static uv_signal_t sigint_watcher;
static uv_signal_t sigterm_watcher;

void oncRPCGetTimeV1Svc(oncRPCSvcReq *req)
{
    uint64_t now = (uint64_t)time(NULL);
    printf("server: GET_TIME over %s\n", req->proto == ONCRPC_PROTO_TCP ? "TCP" : "UDP");
    fflush(stdout);
    oncRPCGetTimeV1Reply(req, &now);
}

/* ---- Registration --------------------------------------------------------- */

static void on_registered(oncRPCServer *srv, oncRPCStat status, void *data)
{
    (void)srv;
    (void)data;
    if (status == ONCRPC_OK)
        printf("server: registered with the port mapper\n");
    else
        printf("server: port mapper registration failed: %s (is rpcbind running?)\n",
               oncRPCStrError(status));
    fflush(stdout);
}

/* ---- Shutdown: unregister, then close --------------------------------------- */

static void on_server_closed(void *data)
{
    (void)data;
    printf("server: closed\n");
}

static void on_unregistered(oncRPCServer *srv, oncRPCStat status, void *data)
{
    (void)data;
    printf("server: unregistered (%s)\n", oncRPCStrError(status));
    oncRPCServerClose(srv, on_server_closed, NULL);
}

static void on_signal(uv_signal_t *watcher, int signum)
{
    (void)watcher;
    (void)signum;
    printf("server: shutting down\n");
    uv_close((uv_handle_t *)&sigint_watcher, NULL);
    uv_close((uv_handle_t *)&sigterm_watcher, NULL);
    if (oncRPCServerPmapUnregister(server, CLOCK_PROG, CLOCK_VERS, on_unregistered, NULL) !=
        ONCRPC_OK)
        oncRPCServerClose(server, on_server_closed, NULL);
}

int main(void)
{
    uv_loop_t *loop = uv_default_loop();

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (oncRPCServerCreate(loop, &app_alloc, &server) != ONCRPC_OK ||
        oncRPCServerListen(server, ONCRPC_PROTO_TCP, NULL, 0) != ONCRPC_OK ||
        oncRPCServerListen(server, ONCRPC_PROTO_UDP, NULL, 0) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot listen\n");
        return 1;
    }
    oncRPCClockProgV1Register(server, NULL);
    printf("server: listening on tcp/%u and udp/%u\n",
           (unsigned)oncRPCServerPort(server, ONCRPC_PROTO_TCP),
           (unsigned)oncRPCServerPort(server, ONCRPC_PROTO_UDP));
    fflush(stdout);

    /* Tell the port mapper about both listeners (asynchronously). */
    oncRPCServerPmapRegister(server, CLOCK_PROG, CLOCK_VERS, on_registered, NULL);

    uv_signal_init(loop, &sigint_watcher);
    uv_signal_start(&sigint_watcher, on_signal, SIGINT);
    uv_signal_init(loop, &sigterm_watcher);
    uv_signal_start(&sigterm_watcher, on_signal, SIGTERM);

    uv_run(loop, UV_RUN_DEFAULT);   /* returns once everything is closed */
    uv_loop_close(loop);
    return 0;
}
