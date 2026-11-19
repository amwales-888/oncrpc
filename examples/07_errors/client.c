/*
 * 07_errors/client.c - credentials, and what each kind of failure looks like.
 *
 * usage: client [host [port]]   (default 127.0.0.1 5007)
 *
 * The steps run one after another; each callback starts the next step:
 *
 *   1. WHOAMI with no credentials (AUTH_NONE)
 *   2. WHOAMI with AUTH_SYS credentials (uid 1000)
 *   3. ADMIN_ONLY as uid 1000          -> ONCRPC_ERR_AUTHERROR
 *   4. procedure 99, which is unknown  -> ONCRPC_ERR_PROCUNAVAIL
 *   5. NO_REPLY with a 500 ms timeout  -> ONCRPC_ERR_TIMEDOUT
 *   6. version 2, which is unknown     -> ONCRPC_ERR_PROGVERSMISMATCH
 *
 * oncRPCClientLastError() gives details inside a callback: the reason for
 * an authentication error, or the versions the server does support.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
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

static uv_loop_t *loop;
static const char *host = "127.0.0.1";
static uint16_t port = 5007;
static char *whoami_text;
static int exit_code;

/* Prints the outcome of a step and remembers if it was not as expected. */
static void report(const char *step, oncRPCStat got, oncRPCStat expected)
{
    printf("client: %-34s %-30s %s\n", step, oncRPCStrError(got),
           got == expected ? "(as expected)" : "(UNEXPECTED)");
    if (got != expected)
        exit_code = 1;
}

/* The steps, in the order they run. */
static void on_step1(oncRPCClient *client, oncRPCStat status, void *result, void *data);
static void on_step2(oncRPCClient *client, oncRPCStat status, void *result, void *data);
static void on_step3(oncRPCClient *client, oncRPCStat status, void *result, void *data);
static void on_step4(oncRPCClient *client, oncRPCStat status, void *result, void *data);
static void on_step5(oncRPCClient *client, oncRPCStat status, void *result, void *data);
static void start_step6(void);
static void on_step6(oncRPCClient *client, oncRPCStat status, void *result, void *data);

/* ---- 1. WHOAMI anonymously ------------------------------------------------- */

static void on_step1(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    uint32_t groups[2] = { 1000, 27 };
    (void)result;
    (void)data;
    report("1. WHOAMI with AUTH_NONE", status, ONCRPC_OK);
    if (status == ONCRPC_OK) {
        printf("client:    server sees: %s\n", whoami_text);
        oncRPCFree(&app_alloc, whoami_text);
    }

    /* From now on, every call carries these AUTH_SYS credentials.
     * (oncRPCClientSetAuthSysDefault() would use this process's identity.) */
    oncRPCClientSetAuthSys(client, "example-host", 1000, 1000, 2, groups);
    if (oncRPCWhoamiV1(client, &whoami_text, on_step2, NULL) != ONCRPC_OK) {
        exit_code = 1;
        oncRPCClientClose(client, NULL, NULL);
    }
}

/* ---- 2. WHOAMI with AUTH_SYS ---------------------------------------------- */

static void on_step2(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    report("2. WHOAMI with AUTH_SYS", status, ONCRPC_OK);
    if (status == ONCRPC_OK) {
        printf("client:    server sees: %s\n", whoami_text);
        oncRPCFree(&app_alloc, whoami_text);
    }

    if (oncRPCAdminOnlyV1(client, on_step3, NULL) != ONCRPC_OK) {
        exit_code = 1;
        oncRPCClientClose(client, NULL, NULL);
    }
}

/* ---- 3. not allowed ------------------------------------------------------- */

static void on_step3(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    report("3. ADMIN_ONLY as uid 1000", status, ONCRPC_ERR_AUTHERROR);
    if (status == ONCRPC_ERR_AUTHERROR)
        printf("client:    reason: auth_stat %d (%s)\n", (int)oncRPCClientLastError(client)->why,
               oncRPCClientLastError(client)->why == ONCRPC_AUTH_TOOWEAK ? "too weak" : "other");

    /* A raw call: procedure 99 is not in secure.x. */
    if (oncRPCClientCall(client, 99, NULL, NULL, NULL, NULL, on_step4, NULL) != ONCRPC_OK) {
        exit_code = 1;
        oncRPCClientClose(client, NULL, NULL);
    }
}

/* ---- 4. unknown procedure ------------------------------------------------- */

static void on_step4(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    report("4. procedure 99", status, ONCRPC_ERR_PROCUNAVAIL);

    oncRPCClientSetTimeout(client, 500);
    if (oncRPCNoReplyV1(client, on_step5, NULL) != ONCRPC_OK) {
        exit_code = 1;
        oncRPCClientClose(client, NULL, NULL);
    }
}

/* ---- 5. no reply: time out ----------------------------------------------- */

static void on_step5(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    report("5. NO_REPLY, 500 ms timeout", status, ONCRPC_ERR_TIMEDOUT);
    oncRPCClientClose(client, NULL, NULL);
    start_step6();
}

/* ---- 6. unknown version --------------------------------------------------- */

static void on_step6(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    const oncRPCError *details = oncRPCClientLastError(client);
    (void)result;
    (void)data;

    report("6. version 2", status, ONCRPC_ERR_PROGVERSMISMATCH);
    if (status == ONCRPC_ERR_PROGVERSMISMATCH)
        printf("client:    server supports versions %u to %u\n",
               (unsigned)details->low, (unsigned)details->high);
    oncRPCClientClose(client, NULL, NULL);
}

static void start_step6(void)
{
    oncRPCClient *v2;

    if (oncRPCClientCreate(loop, &app_alloc, host, port, SECURE_PROG, 2, ONCRPC_PROTO_TCP, NULL,
                           NULL, &v2) != ONCRPC_OK) {
        exit_code = 1;
        return;
    }
    /* Procedure 0 exists in every program: a "ping" with no arguments. */
    if (oncRPCClientCall(v2, 0, NULL, NULL, NULL, NULL, on_step6, NULL) != ONCRPC_OK) {
        exit_code = 1;
        oncRPCClientClose(v2, NULL, NULL);
    }
}

int main(int argc, char **argv)
{
    oncRPCClient *client;
    oncRPCStat status;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    loop = uv_default_loop();
    if (argc > 1)
        host = argv[1];
    if (argc > 2)
        port = (uint16_t)atoi(argv[2]);

    status = oncRPCClientCreate(loop, &app_alloc, host, port, SECURE_PROG, SECURE_VERS,
                                ONCRPC_PROTO_TCP, NULL, NULL, &client);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: %s\n", oncRPCStrError(status));
        return 1;
    }
    if (oncRPCWhoamiV1(client, &whoami_text, on_step1, NULL) != ONCRPC_OK) {
        oncRPCClientClose(client, NULL, NULL);
        exit_code = 1;
    }

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return exit_code;
}
