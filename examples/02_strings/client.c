/*
 * 02_strings/client.c - two calls, one after the other.
 *
 * usage: client [host [port]]   (default 127.0.0.1 5002)
 *
 * The client calls HELLO, then passes HELLO's answer to SHOUT. To run calls
 * in sequence, start the next call from the callback of the previous one.
 *
 * A decoded string is allocated with the client's allocator; free it with
 * oncRPCFree() once you are finished with it.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "greet.h"

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

static text greeting;   /* result of HELLO */
static text shouted;    /* result of SHOUT */
static int exit_code = 1;

static void on_shout_done(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;

    if (status == ONCRPC_OK) {
        printf("client: SHOUT -> \"%s\"\n", shouted);
        oncRPCFree(&app_alloc, shouted);
        exit_code = 0;
    } else {
        printf("client: SHOUT failed: %s\n", oncRPCStrError(status));
    }
    oncRPCClientClose(client, NULL, NULL);
}

static void on_hello_done(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;

    if (status != ONCRPC_OK) {
        printf("client: HELLO failed: %s\n", oncRPCStrError(status));
        oncRPCClientClose(client, NULL, NULL);
        return;
    }
    printf("client: HELLO -> \"%s\"\n", greeting);

    /* Second call. greeting is encoded right here, so it can be freed as
     * soon as oncRPCShoutV1() returns. */
    status = oncRPCShoutV1(client, &greeting, &shouted, on_shout_done, NULL);
    oncRPCFree(&app_alloc, greeting);
    if (status != ONCRPC_OK) {
        printf("client: cannot start SHOUT: %s\n", oncRPCStrError(status));
        oncRPCClientClose(client, NULL, NULL);
    }
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    const char *host = "127.0.0.1";
    uint16_t port = 5002;
    oncRPCClient *client;
    oncRPCStat status;
    text name = "liboncrpc";

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (argc > 1)
        host = argv[1];
    if (argc > 2)
        port = (uint16_t)atoi(argv[2]);

    status = oncRPCClientCreate(loop, &app_alloc, host, port, GREET_PROG, GREET_VERS,
                                ONCRPC_PROTO_TCP, NULL, NULL, &client);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: %s\n", oncRPCStrError(status));
        return 1;
    }

    status = oncRPCHelloV1(client, &name, &greeting, on_hello_done, NULL);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: cannot start HELLO: %s\n", oncRPCStrError(status));
        oncRPCClientClose(client, NULL, NULL);
    }

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return exit_code;
}
