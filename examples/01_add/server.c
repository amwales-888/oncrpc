/*
 * 01_add/server.c - answers ADD requests.
 *
 * usage: server [port]          (default port 5001)
 *
 * oncrpcgen generated add_svc.c from add.x. That file receives requests,
 * decodes the arguments and calls add_1_svc() below. We work out the sum
 * and send it back with add_1_reply().
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>

#include "add.h"

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

/* Called for every ADD request. */
void add_1_svc(const int32_t *a, const int32_t *b, oncRPCSvcReq *req)
{
    int32_t sum = *a + *b;

    printf("server: %d + %d = %d\n", (int)*a, (int)*b, (int)sum);
    fflush(stdout);
    add_1_reply(req, &sum);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    uint16_t port = 5001;
    oncRPCServer *server;

    if (argc > 1)
        port = (uint16_t)atoi(argv[1]);

    if (oncRPCServerCreate(loop, &app_alloc, &server) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot create server\n");
        return 1;
    }
    if (oncRPCServerListen(server, ONCRPC_PROTO_TCP, "127.0.0.1", port) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot listen on port %u\n", (unsigned)port);
        return 1;
    }
    /* Route requests for ADD_PROG version ADD_VERS to add_1_svc(). */
    add_prog_1_register(server, NULL);

    printf("server: listening on 127.0.0.1:%u\n", (unsigned)port);
    fflush(stdout);

    /* Serve until the process is stopped (Ctrl-C). */
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
