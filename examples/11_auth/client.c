/*
 * 11_auth/client.c - calls with a home-made authentication flavor.
 *
 * usage: client [host [port]]   (default 127.0.0.1 5011)
 *
 * The steps run one after another; each callback starts the next step:
 *
 *   1. WHOAMI with no credentials (AUTH_NONE)   -> refused, too weak
 *   2. WHOAMI as alice, with her password       -> "alice"
 *   3. WHOAMI as bob, with a wrong password     -> refused, then the refresh
 *      function supplies the right one and the call goes through
 *   4. SLOW_ECHO, answered 300 ms later
 *   5. SLOW_ECHO on a second connection that is closed before the answer;
 *      then COUNT_ABANDONED shows the server noticed and stopped
 *
 * The token flavor is described in server.c. A client plugs a flavor in
 * with an oncRPCClientAuth: functions that write each call's credential
 * and verifier, check each reply's verifier, and decide whether a refused
 * call should be tried again.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "token.h"

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
static uint16_t port = 5011;
static int exit_code;
static oncRPCClient *client;
static char *text;                 /* string results */
static uint32_t count;
static const uint32_t token_flavor = 390000;

/* ---- The token flavor --------------------------------------------------------- */

/* One login; the auth functions get it as their data pointer. */
typedef struct login {
    const char *user;
    const char *password;
    const char *right_password;    /* what refresh "asks the user" for */
    int         refreshes;
    int         replies_checked;
} login;

/* FNV-1a over a password and then some bytes. Not secure: see server.c. */
static uint32_t checksum(const char *password, const uint8_t *data, size_t len)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; password[i] != '\0'; i++)
        h = (h ^ (uint8_t)password[i]) * 16777619u;
    for (i = 0; i < len; i++)
        h = (h ^ data[i]) * 16777619u;
    return h;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* The credential: "user:password". */
static int token_credential(void *data, uint32_t xid, uint32_t proc, oncRPCAuthData *cred)
{
    login *l = (login *)data;
    int n;
    (void)xid;
    (void)proc;
    n = snprintf((char *)cred->body, sizeof(cred->body), "%s:%s", l->user, l->password);
    if (n < 0 || (size_t)n >= sizeof(cred->body))
        return -1;
    cred->flavor = token_flavor;
    cred->len = (uint32_t)n;
    return 0;
}

/* The verifier: the checksum of the password and the call header. */
static int token_verifier(void *data, const uint8_t *header, size_t len, oncRPCAuthData *verf)
{
    login *l = (login *)data;
    verf->flavor = token_flavor;
    verf->len = 4;
    put_be32(verf->body, checksum(l->password, header, len));
    return 0;
}

/* The server's reply verifier must be the checksum of the password and the
 * xid: only a server that knows the password can produce it. */
static int token_validate(void *data, uint32_t xid, const oncRPCAuthData *verf)
{
    login *l = (login *)data;
    uint8_t x[4], expected[4];
    put_be32(x, xid);
    put_be32(expected, checksum(l->password, x, 4));
    if (verf->flavor != token_flavor || verf->len != 4 || memcmp(verf->body, expected, 4) != 0)
        return -1;
    l->replies_checked++;
    return 0;
}

/* The server refused the credential. A real program might ask the user
 * again; this one knows the right password already. */
static int token_refresh(void *data, oncRPCAuthStat why)
{
    login *l = (login *)data;
    if (why != ONCRPC_AUTH_REJECTEDCRED || l->right_password == NULL)
        return 0;
    l->password = l->right_password;
    l->refreshes++;
    return 1;   /* send the call again */
}

static const oncRPCClientAuth token_auth = {
    token_credential, token_verifier, token_validate, token_refresh, NULL
};

static login alice = { "alice", "wonderland", NULL, 0, 0 };
static login bob = { "bob", "guess", "builder", 0, 0 };

/* ---- Steps ------------------------------------------------------------------------ */

static void report(const char *step, oncRPCStat status, oncRPCStat expected)
{
    printf("client: %-34s %-30s %s\n", step, oncRPCStrError(status),
           status == expected ? "(as expected)" : "(UNEXPECTED)");
    if (status != expected)
        exit_code = 1;
}

static void fail(void)
{
    exit_code = 1;
    oncRPCClientClose(client, NULL, NULL);
}

static void on_step2(oncRPCClient *c, oncRPCStat status, void *result, void *data);
static void on_step3(oncRPCClient *c, oncRPCStat status, void *result, void *data);
static void on_step4(oncRPCClient *c, oncRPCStat status, void *result, void *data);
static void start_step5(void);

static void on_step1(oncRPCClient *c, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    report("1. WHOAMI with AUTH_NONE", status, ONCRPC_ERR_AUTHERROR);
    if (status == ONCRPC_ERR_AUTHERROR)
        printf("client:    reason: %s\n", oncRPCClientLastError(c)->why == ONCRPC_AUTH_TOOWEAK
                                               ? "too weak" : "other");

    /* Every later call carries alice's token. */
    if (oncRPCClientSetAuth(c, &token_auth, &alice) != ONCRPC_OK ||
        oncRPCWhoamiV1(c, &text, on_step2, NULL) != ONCRPC_OK)
        fail();
}

static void on_step2(oncRPCClient *c, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    report("2. WHOAMI as alice", status, ONCRPC_OK);
    if (status == ONCRPC_OK) {
        printf("client:    server says: %s; its reply verifier checked out\n", text);
        oncRPCFree(&app_alloc, text);
        text = NULL;
    }

    /* Now bob, who has typed his password wrong. */
    if (oncRPCClientSetAuth(c, &token_auth, &bob) != ONCRPC_OK ||
        oncRPCWhoamiV1(c, &text, on_step3, NULL) != ONCRPC_OK)
        fail();
}

static void on_step3(oncRPCClient *c, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    report("3. WHOAMI as bob, wrong password", status, ONCRPC_OK);
    if (status == ONCRPC_OK) {
        printf("client:    server says: %s, after %d refresh\n", text, bob.refreshes);
        oncRPCFree(&app_alloc, text);
        text = NULL;
    }
    if (oncRPCSlowEchoV1(c, "hello", &text, on_step4, NULL) != ONCRPC_OK)
        fail();
}

static void on_step4(oncRPCClient *c, oncRPCStat status, void *result, void *data)
{
    (void)c;
    (void)result;
    (void)data;
    report("4. SLOW_ECHO", status, ONCRPC_OK);
    if (status == ONCRPC_OK) {
        printf("client:    answer: %s\n", text);
        oncRPCFree(&app_alloc, text);
        text = NULL;
    }
    start_step5();
}

/* ---- 5. a caller that goes away ------------------------------------------------- */

static oncRPCClient *impatient;
static char *impatient_text;
static uv_timer_t timer;

static void on_count(oncRPCClient *c, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;
    report("5. COUNT_ABANDONED", status, ONCRPC_OK);
    if (status == ONCRPC_OK) {
        printf("client:    the server stopped %u request(s) nobody waited for\n",
               (unsigned)count);
        if (count != 1)
            exit_code = 1;
    }
    uv_close((uv_handle_t *)&timer, NULL);
    oncRPCClientClose(c, NULL, NULL);
}

static void on_impatient_echo(oncRPCClient *c, oncRPCStat status, void *result, void *data)
{
    (void)c;
    (void)result;
    (void)data;
    report("5. SLOW_ECHO, then disconnect", status, ONCRPC_ERR_CANCELLED);
}

/* Fires twice: first to hang up the second client, then to ask the count
 * once the server has had time to notice. */
static void on_timer(uv_timer_t *t)
{
    if (impatient != NULL) {
        oncRPCClientClose(impatient, NULL, NULL);   /* cancels its call */
        impatient = NULL;
        uv_timer_start(t, on_timer, 100, 0);
        return;
    }
    if (oncRPCCountAbandonedV1(client, &count, on_count, NULL) != ONCRPC_OK)
        fail();
}

static void start_step5(void)
{
    if (oncRPCClientCreate(loop, &app_alloc, host, port, TOKEN_PROG, TOKEN_VERS,
                           ONCRPC_PROTO_TCP, NULL, NULL, &impatient) != ONCRPC_OK) {
        fail();
        return;
    }
    if (oncRPCClientSetAuth(impatient, &token_auth, &alice) != ONCRPC_OK ||
        oncRPCSlowEchoV1(impatient, "never mind", &impatient_text, on_impatient_echo,
                         NULL) != ONCRPC_OK) {
        oncRPCClientClose(impatient, NULL, NULL);
        fail();
        return;
    }
    /* Hang up 100 ms later, well before the 300 ms answer. */
    uv_timer_init(loop, &timer);
    uv_timer_start(&timer, on_timer, 100, 0);
}

int main(int argc, char **argv)
{
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

    status = oncRPCClientCreate(loop, &app_alloc, host, port, TOKEN_PROG, TOKEN_VERS,
                                ONCRPC_PROTO_TCP, NULL, NULL, &client);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: %s\n", oncRPCStrError(status));
        return 1;
    }
    if (oncRPCWhoamiV1(client, &text, on_step1, NULL) != ONCRPC_OK)
        fail();

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return exit_code;
}
