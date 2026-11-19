/*
 * 05_async/server.c - replying later, without blocking.
 *
 * usage: server [port]          (default port 5005)
 *
 * oncRPCDelayV1Svc() does not answer straight away. It starts a libuv timer and
 * returns. When the timer fires, the reply is sent. Meanwhile the server
 * keeps handling other requests. A request (and its arguments) stays valid
 * until it is answered.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "delay.h"

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

static uv_loop_t *loop;

/* Everything needed to answer one request later. */
typedef struct pending_reply {
    uv_timer_t    timer;
    oncRPCSvcReq *req;
    uint32_t      ms;
} pending_reply;

static void on_timer_closed(uv_handle_t *handle)
{
    free(handle->data);
}

static void on_timer(uv_timer_t *timer)
{
    pending_reply *p = (pending_reply *)timer->data;

    printf("server: answering DELAY(%u)\n", (unsigned)p->ms);
    fflush(stdout);
    oncRPCDelayV1Reply(p->req, &p->ms);   /* finishes the request */

    uv_close((uv_handle_t *)&p->timer, on_timer_closed);
}

void oncRPCDelayV1Svc(const uint32_t *ms, oncRPCSvcReq *req)
{
    pending_reply *p = (pending_reply *)malloc(sizeof(*p));

    if (p == NULL) {
        oncRPCSvcErrSystem(req);   /* tells the client something went wrong */
        return;
    }
    p->req = req;
    p->ms = *ms;
    uv_timer_init(loop, &p->timer);
    p->timer.data = p;
    uv_timer_start(&p->timer, on_timer, *ms, 0);

    printf("server: DELAY(%u) received, will answer later\n", (unsigned)*ms);
    fflush(stdout);
    /* Returning without a reply is fine: the request stays open. */
}

int main(int argc, char **argv)
{
    uint16_t port = 5005;
    oncRPCServer *server;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    loop = uv_default_loop();
    if (argc > 1)
        port = (uint16_t)atoi(argv[1]);

    if (oncRPCServerCreate(loop, &app_alloc, &server) != ONCRPC_OK ||
        oncRPCServerListen(server, ONCRPC_PROTO_TCP, "127.0.0.1", port) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot listen on port %u\n", (unsigned)port);
        return 1;
    }
    oncRPCDelayProgV1Register(server, NULL);

    printf("server: listening on 127.0.0.1:%u\n", (unsigned)port);
    fflush(stdout);
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
