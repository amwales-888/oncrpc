/*
 * 11_auth/server.c - a home-made authentication flavor, and replies that
 * wait.
 *
 * usage: server [port]          (default port 5011)
 *
 * The "token" flavor (number 390000, from no registry) carries
 * "user:password" in the credential. Each call's verifier is a checksum of
 * the password and the call header, and the server's reply verifier is a
 * checksum of the password and the transaction id, so each side shows it
 * knows the password. The checksum is FNV-1a: this shows how a flavor plugs
 * in, it is NOT secure. For real use, run the calls over TLS (10_tls).
 *
 * The authenticator runs before any procedure. It refuses AUTH_NONE and
 * AUTH_SYS callers, checks the token, and leaves the user's name in
 * req->authCtx for the procedures.
 *
 * SLOW_ECHO answers after 300 ms. If its caller disconnects first, the
 * library calls on_abandoned(): the timer is stopped and the request
 * dropped, instead of doing work nobody will see.
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
static const uint32_t token_flavor = 390000;
static uint32_t abandoned_count;

/* Who may call. */
typedef struct user {
    const char *name;
    const char *password;
} user;

static const user users[] = {
    { "alice", "wonderland" },
    { "bob",   "builder" }
};

/* FNV-1a over a password and then some bytes. */
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

/* ---- Authentication ------------------------------------------------------- */

static oncRPCAuthStat refuse_others(oncRPCSvcReq *req, const uint8_t *header, size_t len,
                                    void *ctx)
{
    (void)header;
    (void)len;
    (void)ctx;
    printf("server: refused a caller with flavor %u\n", (unsigned)req->credFlavor);
    fflush(stdout);
    return ONCRPC_AUTH_TOOWEAK;
}

static oncRPCAuthStat check_token(oncRPCSvcReq *req, const uint8_t *header, size_t len,
                                  void *ctx)
{
    const oncRPCAuthData *cred = oncRPCSvcCred(req);
    const oncRPCAuthData *verf = oncRPCSvcVerf(req);
    const user *u = NULL;
    uint8_t expected[4], xid[4], reply[4];
    size_t i, namelen;
    (void)ctx;

    /* The credential: "user:password". */
    for (i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        namelen = strlen(users[i].name);
        if (cred->len == namelen + 1 + strlen(users[i].password) &&
            memcmp(cred->body, users[i].name, namelen) == 0 && cred->body[namelen] == ':' &&
            memcmp(cred->body + namelen + 1, users[i].password, cred->len - namelen - 1) == 0)
            u = &users[i];
    }
    if (u == NULL) {
        printf("server: bad user name or password\n");
        fflush(stdout);
        return ONCRPC_AUTH_REJECTEDCRED;
    }

    /* The verifier: the checksum of the password and the header. */
    put_be32(expected, checksum(u->password, header, len));
    if (verf->flavor != token_flavor || verf->len != 4 || memcmp(verf->body, expected, 4) != 0)
        return ONCRPC_AUTH_BADVERF;

    /* Our answer: the checksum of the password and the xid. */
    put_be32(xid, req->xid);
    put_be32(reply, checksum(u->password, xid, 4));
    if (oncRPCSvcSetReplyVerf(req, token_flavor, reply, 4) != ONCRPC_OK)
        return ONCRPC_AUTH_FAILED;

    req->authCtx = (void *)u;   /* for the procedures */
    return ONCRPC_AUTH_OK;
}

/* ---- Procedures ------------------------------------------------------------ */

void oncRPCWhoamiV1Svc(oncRPCSvcReq *req)
{
    const user *u = (const user *)req->authCtx;
    printf("server: WHOAMI from %s\n", u->name);
    fflush(stdout);
    oncRPCWhoamiV1Reply(req, u->name);
}

/* Everything needed to answer one SLOW_ECHO later. */
typedef struct pending_echo {
    uv_timer_t    timer;
    oncRPCSvcReq *req;
    const char   *text;   /* the argument: valid until the request is finished */
} pending_echo;

static void on_timer_closed(uv_handle_t *handle)
{
    free(handle->data);
}

static void on_echo_timer(uv_timer_t *timer)
{
    pending_echo *p = (pending_echo *)timer->data;
    printf("server: SLOW_ECHO answers \"%s\"\n", p->text);
    fflush(stdout);
    oncRPCSlowEchoV1Reply(p->req, p->text);
    uv_close((uv_handle_t *)&p->timer, on_timer_closed);
}

/* The caller went away before the answer was ready. */
static void on_abandoned(oncRPCSvcReq *req, void *data)
{
    pending_echo *p = (pending_echo *)data;
    printf("server: SLOW_ECHO \"%s\": caller went away, stopped\n", p->text);
    fflush(stdout);
    abandoned_count++;
    oncRPCSvcDrop(req);            /* finished: no reply */
    uv_close((uv_handle_t *)&p->timer, on_timer_closed);   /* also stops it */
}

void oncRPCSlowEchoV1Svc(const char *text, oncRPCSvcReq *req)
{
    pending_echo *p = (pending_echo *)malloc(sizeof(*p));
    if (p == NULL) {
        oncRPCSvcErrSystem(req);
        return;
    }
    p->req = req;
    p->text = text;
    uv_timer_init(loop, &p->timer);
    p->timer.data = p;
    uv_timer_start(&p->timer, on_echo_timer, 300, 0);
    oncRPCSvcSetAbandonCb(req, on_abandoned, p);
}

void oncRPCCountAbandonedV1Svc(oncRPCSvcReq *req)
{
    oncRPCCountAbandonedV1Reply(req, &abandoned_count);
}

/* ---- main --------------------------------------------------------------------- */

static oncRPCServer *server;
static uv_signal_t sigint, sigterm;

static void on_signal(uv_signal_t *handle, int signum)
{
    (void)handle;
    (void)signum;
    uv_close((uv_handle_t *)&sigint, NULL);
    uv_close((uv_handle_t *)&sigterm, NULL);
    oncRPCServerClose(server, NULL, NULL);
}

int main(int argc, char **argv)
{
    uint16_t port = 5011;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    if (argc > 1)
        port = (uint16_t)atoi(argv[1]);
    loop = uv_default_loop();

    if (oncRPCServerCreate(loop, &app_alloc, &server) != ONCRPC_OK ||
        oncRPCServerListen(server, ONCRPC_PROTO_TCP, "127.0.0.1", port) != ONCRPC_OK ||
        oncRPCServerSetAuth(server, token_flavor, check_token, NULL) != ONCRPC_OK ||
        oncRPCServerSetAuth(server, ONCRPC_AUTH_NONE, refuse_others, NULL) != ONCRPC_OK ||
        oncRPCServerSetAuth(server, ONCRPC_AUTH_SYS, refuse_others, NULL) != ONCRPC_OK ||
        oncRPCTokenProgV1Register(server, NULL) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot start on port %u\n", (unsigned)port);
        return 1;
    }
    printf("server: listening on 127.0.0.1:%u\n", (unsigned)port);
    fflush(stdout);

    uv_signal_init(loop, &sigint);
    uv_signal_start(&sigint, on_signal, SIGINT);
    uv_signal_init(loop, &sigterm);
    uv_signal_start(&sigterm, on_signal, SIGTERM);
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return 0;
}
