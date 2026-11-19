/*
 * 05_async/client.c - answers arrive in the order they are ready.
 *
 * usage: client [host [port]]   (default 127.0.0.1 5005)
 *
 * Three DELAY calls (300, 100 and 200 ms) go out together on one connection.
 * The replies come back after about 100, 200 and 300 ms, in that order,
 * because the server answers each one when its timer fires. Altogether
 * this takes about 300 ms, not 600.
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
static uint64_t start_ms;
static uint32_t answers[3];
static uint32_t answer_order[3];
static int answered;
static int exit_code;

static void on_delay_done(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    uint32_t *answer = (uint32_t *)result;   /* one of answers[] */
    (void)data;

    if (status == ONCRPC_OK) {
        printf("client: DELAY(%u) answered after %u ms\n", (unsigned)*answer,
               (unsigned)(uv_now(loop) - start_ms));
        answer_order[answered] = *answer;
    } else {
        printf("client: DELAY failed: %s\n", oncRPCStrError(status));
        exit_code = 1;
    }
    answered++;
    if (answered == 3) {
        if (answer_order[0] != 100 || answer_order[1] != 200 || answer_order[2] != 300)
            exit_code = 1;
        oncRPCClientClose(client, NULL, NULL);
    }
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    uint16_t port = 5005;
    oncRPCClient *client;
    oncRPCStat status;
    uint32_t delays[3] = { 300, 100, 200 };
    int i;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    loop = uv_default_loop();
    if (argc > 1)
        host = argv[1];
    if (argc > 2)
        port = (uint16_t)atoi(argv[2]);

    status = oncRPCClientCreate(loop, &app_alloc, host, port, DELAY_PROG, DELAY_VERS,
                                ONCRPC_PROTO_TCP, NULL, NULL, &client);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: %s\n", oncRPCStrError(status));
        return 1;
    }

    uv_update_time(loop);
    start_ms = uv_now(loop);
    for (i = 0; i < 3; i++) {
        printf("client: sending DELAY(%u)\n", (unsigned)delays[i]);
        status = oncRPCDelayV1(client, &delays[i], &answers[i], on_delay_done, NULL);
        if (status != ONCRPC_OK) {
            printf("client: cannot send DELAY: %s\n", oncRPCStrError(status));
            exit_code = 1;
            answered++;
        }
    }
    if (answered == 3)
        oncRPCClientClose(client, NULL, NULL);

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return exit_code;
}
