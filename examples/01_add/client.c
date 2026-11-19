/*
 * 01_add/client.c - asks the server to add two numbers.
 *
 * usage: client [host [port]]   (default 127.0.0.1 5001)
 *
 * Calls are asynchronous: oncRPCAddV1() sends the request and returns at once.
 * When the answer arrives, libuv runs on_add_done(). uv_run() keeps the
 * program going until then.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "add.h"

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

/* The result is written here, so it must still exist when the reply comes. */
static int32_t sum;
static int exit_code = 1;

/* Runs when the ADD call has finished, successfully or not. */
static void on_add_done(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;   /* points at sum */
    (void)data;

    if (status == ONCRPC_OK) {
        printf("client: 2 + 3 = %d\n", (int)sum);
        exit_code = 0;
    } else {
        printf("client: ADD failed: %s\n", oncRPCStrError(status));
    }
    /* Nothing else to do: close the client. uv_run() then returns. */
    oncRPCClientClose(client, NULL, NULL);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    const char *host = "127.0.0.1";
    uint16_t port = 5001;
    oncRPCClient *client;
    oncRPCStat status;
    int32_t a = 2;
    int32_t b = 3;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (argc > 1)
        host = argv[1];
    if (argc > 2)
        port = (uint16_t)atoi(argv[2]);

    /* Connects in the background; calls made before then are queued. */
    status = oncRPCClientCreate(loop, &app_alloc, host, port, ADD_PROG, ADD_VERS,
                                ONCRPC_PROTO_TCP, NULL, NULL, &client);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: %s\n", oncRPCStrError(status));
        return 1;
    }

    /* a and b are encoded before oncRPCAddV1() returns; sum is filled in later. */
    status = oncRPCAddV1(client, &a, &b, &sum, on_add_done, NULL);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: cannot start ADD: %s\n", oncRPCStrError(status));
        oncRPCClientClose(client, NULL, NULL);
    }

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return exit_code;
}
