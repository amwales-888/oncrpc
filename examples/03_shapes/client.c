/*
 * 03_shapes/client.c - several calls in flight at once.
 *
 * usage: client [host [port]]   (default 127.0.0.1 5003)
 *
 * All four AREA calls are started immediately, one after another, without
 * waiting. Each call has its own result storage. The data pointer passed
 * to area_1() comes back in the callback, so the callback knows which call
 * finished. A counter tells us when the last one is done.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

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

/* One outstanding question and the place its answer goes. */
typedef struct question {
    const char  *description;
    shape        shape;
    area_result  result;
} question;

static question questions[4];
static int outstanding;
static int exit_code;

static void on_area_done(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    question *q = (question *)data;
    (void)result;   /* the same as &q->result */

    if (status != ONCRPC_OK) {
        printf("client: %-30s call failed: %s\n", q->description, oncRPCStrError(status));
        exit_code = 1;
    } else if (q->result.ok) {
        printf("client: %-30s area %.4f\n", q->description, q->result.area_result_u.area);
    } else {
        printf("client: %-30s error: %s\n", q->description, q->result.area_result_u.error);
    }
    /* The error string was allocated while decoding: release the result. */
    if (status == ONCRPC_OK)
        xdr_free_area_result(&app_alloc, &q->result);

    outstanding--;
    if (outstanding == 0)
        oncRPCClientClose(client, NULL, NULL);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    const char *host = "127.0.0.1";
    uint16_t port = 5003;
    oncRPCClient *client;
    oncRPCStat status;
    int i;

    if (argc > 1)
        host = argv[1];
    if (argc > 2)
        port = (uint16_t)atoi(argv[2]);

    memset(questions, 0, sizeof(questions));
    questions[0].description = "circle, radius 1";
    questions[0].shape.kind = CIRCLE;
    questions[0].shape.shape_u.radius = 1.0;

    questions[1].description = "rectangle 3 x 4";
    questions[1].shape.kind = RECTANGLE;
    questions[1].shape.shape_u.rect.width = 3.0;
    questions[1].shape.shape_u.rect.height = 4.0;

    questions[2].description = "triangle 3, 4, 5";
    questions[2].shape.kind = TRIANGLE;
    questions[2].shape.shape_u.tri.a = 3.0;
    questions[2].shape.shape_u.tri.b = 4.0;
    questions[2].shape.shape_u.tri.c = 5.0;

    questions[3].description = "triangle 1, 1, 5";
    questions[3].shape.kind = TRIANGLE;
    questions[3].shape.shape_u.tri.a = 1.0;
    questions[3].shape.shape_u.tri.b = 1.0;
    questions[3].shape.shape_u.tri.c = 5.0;

    status = oncRPCClientCreate(loop, &app_alloc, host, port, SHAPES_PROG, SHAPES_VERS,
                                ONCRPC_PROTO_TCP, NULL, NULL, &client);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: %s\n", oncRPCStrError(status));
        return 1;
    }

    for (i = 0; i < 4; i++) {
        status = area_1(client, &questions[i].shape, &questions[i].result, on_area_done,
                        &questions[i]);
        if (status == ONCRPC_OK) {
            outstanding++;
        } else {
            printf("client: cannot ask about %s: %s\n", questions[i].description,
                   oncRPCStrError(status));
            exit_code = 1;
        }
    }
    if (outstanding == 0)
        oncRPCClientClose(client, NULL, NULL);

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return exit_code;
}
