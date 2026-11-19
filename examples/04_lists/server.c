/*
 * 04_lists/server.c - summarises an array of numbers and sorts a list of words.
 *
 * usage: server [port]          (default port 5004)
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lists.h"

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

void summarize_1_svc(const samples *s, oncRPCSvcReq *req)
{
    summary result;
    double total = 0.0;
    uint32_t i;

    memset(&result, 0, sizeof(result));
    result.count = s->samples_len;
    for (i = 0; i < s->samples_len; i++) {
        double v = s->samples_val[i];
        if (i == 0 || v < result.min)
            result.min = v;
        if (i == 0 || v > result.max)
            result.max = v;
        total += v;
    }
    if (s->samples_len > 0)
        result.mean = total / s->samples_len;

    printf("server: SUMMARIZE(%u numbers)\n", (unsigned)s->samples_len);
    fflush(stdout);
    summarize_1_reply(req, &result);
}

static int compare_words(const void *x, const void *y)
{
    const word *a = *(const word *const *)x;
    const word *b = *(const word *const *)y;
    return strcmp(a->text, b->text);
}

enum { MAX_SORT = 100 };   /* how many words this server is willing to sort */

void sort_words_1_svc(const word_list *list, oncRPCSvcReq *req)
{
    const word *order[MAX_SORT];
    word sorted[MAX_SORT];
    word_list result = NULL;
    const word *w;
    size_t n = 0, i;

    /* Collect the words, sort the pointers, then link a new list in a
     * local array. The reply is encoded immediately, so stack memory is
     * fine. */
    for (w = *list; w != NULL; w = w->next) {
        if (n == MAX_SORT) {
            oncRPCSvcErrSystem(req);
            return;
        }
        order[n++] = w;
    }
    qsort((void *)order, n, sizeof(order[0]), compare_words);
    for (i = n; i > 0; i--) {
        sorted[i - 1].text = order[i - 1]->text;
        sorted[i - 1].next = result;
        result = &sorted[i - 1];
    }

    printf("server: SORT_WORDS(%u words)\n", (unsigned)n);
    fflush(stdout);
    sort_words_1_reply(req, &result);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    uint16_t port = 5004;
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
    lists_prog_1_register(server, NULL);

    printf("server: listening on 127.0.0.1:%u\n", (unsigned)port);
    fflush(stdout);
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
