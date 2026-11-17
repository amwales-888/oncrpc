/*
 * 07_errors/server.c - looks at credentials and answers with errors.
 *
 * usage: server [port]          (default port 5007)
 *
 * Every request carries credentials. With AUTH_NONE the caller is
 * anonymous. With AUTH_SYS, req->authSys holds the machine name, uid, gid
 * and groups the client claims. (AUTH_SYS is not proof of identity; it is
 * what NFS-style services traditionally trust.)
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>

#include "secure.h"

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

void whoami_1_svc(oncRPCSvcReq *req)
{
    char text[400];

    if (req->authSys == NULL)
        snprintf(text, sizeof(text), "anonymous (AUTH_NONE)");
    else
        snprintf(text, sizeof(text), "uid %u, gid %u on %s (AUTH_SYS)",
                 (unsigned)req->authSys->uid, (unsigned)req->authSys->gid,
                 req->authSys->machinename);
    printf("server: WHOAMI -> %s\n", text);
    fflush(stdout);
    whoami_1_reply(req, text);
}

void admin_only_1_svc(oncRPCSvcReq *req)
{
    if (req->authSys == NULL || req->authSys->uid != 0) {
        /* Reject the credentials: the client sees ONCRPC_ERR_AUTHERROR. */
        printf("server: ADMIN_ONLY refused\n");
        fflush(stdout);
        oncRPCSvcErrAuth(req, ONCRPC_AUTH_TOOWEAK);
        return;
    }
    printf("server: ADMIN_ONLY allowed\n");
    fflush(stdout);
    admin_only_1_reply(req);
}

void no_reply_1_svc(oncRPCSvcReq *req)
{
    /* Finish the request without answering: the client will time out. */
    printf("server: NO_REPLY dropped\n");
    fflush(stdout);
    oncRPCSvcDrop(req);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    uint16_t port = 5007;
    oncRPCServer *server;

    if (argc > 1)
        port = (uint16_t)atoi(argv[1]);

    if (oncRPCServerCreate(loop, &app_alloc, &server) != ONCRPC_OK ||
        oncRPCServerListen(server, ONCRPC_PROTO_TCP, "127.0.0.1", port) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot listen on port %u\n", (unsigned)port);
        return 1;
    }
    /* Only version 1 is served. Requests for other versions are answered
     * with PROG_MISMATCH automatically, unknown procedures with
     * PROC_UNAVAIL. */
    secure_prog_1_register(server, NULL);

    printf("server: listening on 127.0.0.1:%u\n", (unsigned)port);
    fflush(stdout);
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
