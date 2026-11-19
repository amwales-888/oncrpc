/*
 * 04_lists/client.c - sends an array and a linked list.
 *
 * usage: client [host [port]]   (default 127.0.0.1 5004)
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

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

static summary numbers_summary;
static word_list sorted_words;
static int outstanding;
static int exit_code;

static void call_finished(oncRPCClient *client)
{
    outstanding--;
    if (outstanding == 0)
        oncRPCClientClose(client, NULL, NULL);
}

static void on_summarize_done(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;

    if (status == ONCRPC_OK) {
        printf("client: %u numbers, min %g, max %g, mean %g\n",
               (unsigned)numbers_summary.count, numbers_summary.min, numbers_summary.max,
               numbers_summary.mean);
    } else {
        printf("client: SUMMARIZE failed: %s\n", oncRPCStrError(status));
        exit_code = 1;
    }
    call_finished(client);
}

static void on_sort_done(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    const word *w;
    (void)result;
    (void)data;

    if (status == ONCRPC_OK) {
        printf("client: sorted:");
        for (w = sorted_words; w != NULL; w = w->next)
            printf(" %s", w->text);
        printf("\n");
        /* Frees every node and every string of the decoded list. */
        xdr_free_word_list(&app_alloc, &sorted_words);
    } else {
        printf("client: SORT_WORDS failed: %s\n", oncRPCStrError(status));
        exit_code = 1;
    }
    call_finished(client);
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    const char *host = "127.0.0.1";
    uint16_t port = 5004;
    oncRPCClient *client;
    oncRPCStat status;

#ifdef SIGPIPE
    /* A peer that closes its end must not kill the program (USAGE.md, "Signals"). */
    signal(SIGPIPE, SIG_IGN);
#endif
    /* An array argument: point samples_val at the data, set samples_len. */
    double values[6] = { 4.5, -2.0, 10.0, 3.25, 7.0, 0.5 };
    samples numbers;

    /* A linked list argument, built from local nodes. */
    word w4 = { "delta", NULL };
    word w3 = { "alpha", &w4 };
    word w2 = { "charlie", &w3 };
    word w1 = { "bravo", &w2 };
    word_list words = &w1;

    numbers.samples_len = 6;
    numbers.samples_val = values;

    if (argc > 1)
        host = argv[1];
    if (argc > 2)
        port = (uint16_t)atoi(argv[2]);

    status = oncRPCClientCreate(loop, &app_alloc, host, port, LISTS_PROG, LISTS_VERS,
                                ONCRPC_PROTO_TCP, NULL, NULL, &client);
    if (status != ONCRPC_OK) {
        fprintf(stderr, "client: %s\n", oncRPCStrError(status));
        return 1;
    }

    /* Both arguments are encoded during the calls: the local arrays and
     * nodes above do not need to outlive them. */
    status = summarize_1(client, &numbers, &numbers_summary, on_summarize_done, NULL);
    if (status == ONCRPC_OK)
        outstanding++;
    else
        exit_code = 1;

    status = sort_words_1(client, &words, &sorted_words, on_sort_done, NULL);
    if (status == ONCRPC_OK)
        outstanding++;
    else
        exit_code = 1;

    if (outstanding == 0)
        oncRPCClientClose(client, NULL, NULL);

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return exit_code;
}
