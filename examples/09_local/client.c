/*
 * 09_local/client.c - asks the server on a local socket who we are.
 *
 * usage: client [path]   (default /tmp/oncrpc-09.sock; on Windows the pipe
 *                         \\.\pipe\oncrpc-09)
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "local.h"

#ifdef _WIN32
#define DEFAULT_PATH "\\\\.\\pipe\\oncrpc-09"
#else
#define DEFAULT_PATH "/tmp/oncrpc-09.sock"
#endif

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

static char *answer;   /* the decoded string; ours to free */
static int   failed;

static void on_who_am_i(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;   /* == &answer */
    (void)data;
    if (status == ONCRPC_OK) {
        printf("client: the server sees %s\n", answer);
        oncRPCFree(&app_alloc, answer);
    } else {
        printf("client: WHO_AM_I failed: %s\n", oncRPCStrError(status));
        failed = 1;
    }
    oncRPCClientClose(client, NULL, NULL);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    const char *path = DEFAULT_PATH;
    oncRPCClient *client;
    oncRPCStat status;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (argc > 1)
        path = argv[1];

    /* The "host" is the socket's path; the port is not used. */
    status = oncRPCClientCreate(loop, &app_alloc, path, 0, LOCAL_PROG, LOCAL_VERS,
                                ONCRPC_PROTO_LOCAL, NULL, NULL, &client);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: %s\n", oncRPCStrError(status));
        return 1;
    }
    oncRPCWhoAmIV1(client, &answer, on_who_am_i, NULL);

    uv_run(loop, UV_RUN_DEFAULT);
    return failed;
}
