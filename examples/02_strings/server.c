/*
 * 02_strings/server.c - string arguments and results.
 *
 * usage: server [port]          (default port 5002)
 *
 * A reply is encoded the moment it is sent and is not kept afterwards, so
 * a result may live in a local buffer on the stack.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* "text" is a typedef for char *, so the argument arrives as text *. */
void oncRPCHelloV1Svc(const text *name, oncRPCSvcReq *req)
{
    char reply[MAX_TEXT + 1];
    text result = reply;

    snprintf(reply, sizeof(reply), "Hello, %s!", *name);
    printf("server: HELLO(\"%s\") -> \"%s\"\n", *name, reply);
    fflush(stdout);
    oncRPCHelloV1Reply(req, &result);
}

void oncRPCShoutV1Svc(const text *message, oncRPCSvcReq *req)
{
    char reply[MAX_TEXT + 1];
    text result = reply;
    size_t i;

    for (i = 0; (*message)[i] != '\0' && i < MAX_TEXT; i++)
        reply[i] = (char)toupper((unsigned char)(*message)[i]);
    reply[i] = '\0';
    printf("server: SHOUT(\"%s\") -> \"%s\"\n", *message, reply);
    fflush(stdout);
    oncRPCShoutV1Reply(req, &result);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    uint16_t port = 5002;
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
    oncRPCGreetProgV1Register(server, NULL);

    printf("server: listening on 127.0.0.1:%u\n", (unsigned)port);
    fflush(stdout);
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
