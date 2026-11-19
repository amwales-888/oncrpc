/*
 * 06_portmap/client.c - finds the server through the port mapper.
 *
 * usage: client [host]          (default 127.0.0.1)
 *
 * Port 0 in oncRPCClientCreate() means: ask the port mapper on the host
 * where CLOCK_PROG version CLOCK_VERS is. The client does this once over
 * TCP and once over UDP, and asks each for the time.
 *
 * The optional "ready" callback reports when a client has found and
 * reached its server, or why it could not.
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

static uint64_t tcp_time;
static uint64_t udp_time;
static int exit_code;

static void on_ready(oncRPCClient *client, oncRPCStat status, void *data)
{
    const char *transport = (const char *)data;
    (void)client;
    if (status == ONCRPC_OK)
        printf("client: %s client is connected\n", transport);
    else
        printf("client: %s client cannot reach the server: %s\n", transport,
               oncRPCStrError(status));
}

static void on_time(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    const char *transport = (const char *)data;
    uint64_t *seconds = (uint64_t *)result;

    if (status == ONCRPC_OK) {
        time_t t = (time_t)*seconds;
        char text[64];
        strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S UTC", gmtime(&t));
        printf("client: over %s the server says it is %s\n", transport, text);
    } else {
        printf("client: GET_TIME over %s failed: %s\n", transport, oncRPCStrError(status));
        exit_code = 1;
    }
    oncRPCClientClose(client, NULL, NULL);
}

static void ask_time(uv_loop_t *loop, const char *host, oncRPCProto proto, uint64_t *result)
{
    const char *name = proto == ONCRPC_PROTO_TCP ? "TCP" : "UDP";
    oncRPCClient *client;
    oncRPCStat status;

    status = oncRPCClientCreate(loop, &app_alloc, host, 0 /* ask the port mapper */,
                                CLOCK_PROG, CLOCK_VERS, proto, on_ready, (void *)name, &client);
    if (status != ONCRPC_OK) {
        printf("client: %s: %s\n", name, oncRPCStrError(status));
        exit_code = 1;
        return;
    }
    status = get_time_1(client, result, on_time, (void *)name);
    if (status != ONCRPC_OK) {
        printf("client: cannot ask over %s: %s\n", name, oncRPCStrError(status));
        exit_code = 1;
        oncRPCClientClose(client, NULL, NULL);
    }
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    const char *host = "127.0.0.1";

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (argc > 1)
        host = argv[1];

    ask_time(loop, host, ONCRPC_PROTO_TCP, &tcp_time);
    ask_time(loop, host, ONCRPC_PROTO_UDP, &udp_time);

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return exit_code;
}
