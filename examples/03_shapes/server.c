/*
 * 03_shapes/server.c - computes the area of a circle, rectangle or triangle.
 *
 * usage: server [port]          (default port 5003)
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shapes.h"

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

static const double pi = 3.14159265358979323846;

void area_1_svc(const shape *s, oncRPCSvcReq *req)
{
    area_result result;

    memset(&result, 0, sizeof(result));
    result.ok = ONCRPC_TRUE;

    switch (s->kind) {
    case CIRCLE:
        result.area_result_u.area = pi * s->shape_u.radius * s->shape_u.radius;
        break;
    case RECTANGLE:
        result.area_result_u.area = s->shape_u.rect.width * s->shape_u.rect.height;
        break;
    case TRIANGLE: {
        /* Heron's formula */
        double a = s->shape_u.tri.a, b = s->shape_u.tri.b, c = s->shape_u.tri.c;
        double p = (a + b + c) / 2.0;
        double q = p * (p - a) * (p - b) * (p - c);
        if (a <= 0 || b <= 0 || c <= 0 || q <= 0) {
            result.ok = ONCRPC_FALSE;
            result.area_result_u.error = "these sides do not make a triangle";
        } else {
            result.area_result_u.area = sqrt(q);
        }
        break;
    }
    default:
        result.ok = ONCRPC_FALSE;
        result.area_result_u.error = "unknown shape";
        break;
    }

    printf("server: AREA(kind %d) -> %s\n", (int)s->kind, result.ok ? "ok" : "error");
    fflush(stdout);
    area_1_reply(req, &result);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    uint16_t port = 5003;
    oncRPCServer *server;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (argc > 1)
        port = (uint16_t)atoi(argv[1]);

    if (oncRPCServerCreate(loop, &app_alloc, &server) != ONCRPC_OK ||
        oncRPCServerListen(server, ONCRPC_PROTO_TCP, "127.0.0.1", port) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot listen on port %u\n", (unsigned)port);
        return 1;
    }
    shapes_prog_1_register(server, NULL);

    printf("server: listening on 127.0.0.1:%u\n", (unsigned)port);
    fflush(stdout);
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
