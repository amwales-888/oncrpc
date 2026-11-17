/*
 * test_xdr.c - unit tests for the XDR primitives and message encoding.
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oncrpc.h"

/* Counting allocator: every block must be returned by the end. */
static long g_live;
static void *t_realloc(void *ctx, void *p, size_t n)
{
    void *q = realloc(p, n);
    (void)ctx;
    if (p == NULL && q != NULL)
        g_live++;
    return q;
}
static void t_free(void *ctx, void *p)
{
    (void)ctx;
    g_live--;
    free(p);
}
static const oncRPCAllocator A = { t_realloc, t_free, NULL };

static int g_fail, g_count;

#define CHECK(cond) do { g_count++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int bytes_eq(const oncRPCXdr *x, const uint8_t *exp, size_t n)
{
    return oncRPCXdrLength(x) == n && memcmp(oncRPCXdrData(x), exp, n) == 0;
}

static void test_integers(void)
{
    uint8_t buf[64];
    oncRPCXdr x;
    int32_t i32 = -2;
    uint32_t u32 = 0xdeadbeefu;
    int64_t i64 = -3;
    uint64_t u64 = 0x0102030405060708ull;
    static const uint8_t exp[] = {
        0xff, 0xff, 0xff, 0xfe,
        0xde, 0xad, 0xbe, 0xef,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };

    oncRPCXdrInitEncode(&x, buf, sizeof(buf));
    CHECK(oncRPCXdrInt32(&x, &i32));
    CHECK(oncRPCXdrUint32(&x, &u32));
    CHECK(oncRPCXdrInt64(&x, &i64));
    CHECK(oncRPCXdrUint64(&x, &u64));
    CHECK(bytes_eq(&x, exp, sizeof(exp)));

    i32 = 0; u32 = 0; i64 = 0; u64 = 0;
    oncRPCXdrInitDecode(&x, &A, exp, sizeof(exp));
    CHECK(oncRPCXdrInt32(&x, &i32) && i32 == -2);
    CHECK(oncRPCXdrUint32(&x, &u32) && u32 == 0xdeadbeefu);
    CHECK(oncRPCXdrInt64(&x, &i64) && i64 == -3);
    CHECK(oncRPCXdrUint64(&x, &u64) && u64 == 0x0102030405060708ull);
    CHECK(oncRPCXdrRemaining(&x) == 0);
    CHECK(!oncRPCXdrInt32(&x, &i32));     /* past the end */

    /* extremes */
    {
        int32_t mn = INT32_MIN, out;
        int64_t mn64 = INT64_MIN, out64;
        oncRPCXdrInitEncode(&x, buf, sizeof(buf));
        CHECK(oncRPCXdrInt32(&x, &mn) && oncRPCXdrInt64(&x, &mn64));
        oncRPCXdrInitDecode(&x, &A, buf, oncRPCXdrLength(&x));
        CHECK(oncRPCXdrInt32(&x, &out) && out == INT32_MIN);
        CHECK(oncRPCXdrInt64(&x, &out64) && out64 == INT64_MIN);
    }
}

static void test_small_and_bool(void)
{
    uint8_t buf[64];
    oncRPCXdr x;
    int8_t c = -5;
    uint16_t us = 65535;
    oncRPCBool b = 7;   /* any non-zero encodes as 1 */
    static const uint8_t two[] = { 0, 0, 0, 2 };
    static const uint8_t big[] = { 0, 0, 1, 0 };

    oncRPCXdrInitEncode(&x, buf, sizeof(buf));
    CHECK(oncRPCXdrInt8(&x, &c) && oncRPCXdrUint16(&x, &us) && oncRPCXdrBool(&x, &b));
    CHECK(oncRPCXdrLength(&x) == 12 && buf[11] == 1 && buf[3] == 0xfb && buf[0] == 0xff);
    c = 0; us = 0; b = 0;
    oncRPCXdrInitDecode(&x, &A, buf, 12);
    CHECK(oncRPCXdrInt8(&x, &c) && c == -5);
    CHECK(oncRPCXdrUint16(&x, &us) && us == 65535);
    CHECK(oncRPCXdrBool(&x, &b) && b == 1);

    oncRPCXdrInitDecode(&x, &A, two, 4);
    CHECK(!oncRPCXdrBool(&x, &b));          /* booleans must be 0 or 1 */
    oncRPCXdrInitDecode(&x, &A, big, 4);
    {
        uint8_t u8;
        CHECK(!oncRPCXdrUint8(&x, &u8));    /* 256 does not fit */
    }
}

static void test_float(void)
{
    uint8_t buf[16];
    oncRPCXdr x;
    float f = 1.5f;
    double d = -2.25;
    static const uint8_t exp[] = { 0x3f, 0xc0, 0, 0, 0xc0, 0x02, 0, 0, 0, 0, 0, 0 };

    oncRPCXdrInitEncode(&x, buf, sizeof(buf));
    CHECK(oncRPCXdrFloat(&x, &f) && oncRPCXdrDouble(&x, &d));
    CHECK(bytes_eq(&x, exp, sizeof(exp)));
    f = 0; d = 0;
    oncRPCXdrInitDecode(&x, &A, exp, sizeof(exp));
    CHECK(oncRPCXdrFloat(&x, &f) && f == 1.5f);
    CHECK(oncRPCXdrDouble(&x, &d) && d == -2.25);
}

static void test_opaque_and_string(void)
{
    oncRPCXdr x;
    uint8_t fixed[5] = { 1, 2, 3, 4, 5 }, back[5];
    uint8_t *var = (uint8_t *)"abc", *vback = NULL;
    uint32_t vlen = 3, vbacklen = 0;
    char *s = "hello", *sback = NULL, *null_s = NULL;
    static const uint8_t exp[] = {
        1, 2, 3, 4, 5, 0, 0, 0,               /* opaque[5] + 3 pad */
        0, 0, 0, 3, 'a', 'b', 'c', 0,         /* opaque<> */
        0, 0, 0, 5, 'h', 'e', 'l', 'l', 'o', 0, 0, 0,
        0, 0, 0, 0                            /* NULL string -> "" */
    };

    CHECK(oncRPCXdrInitEncodeDynamic(&x, &A, 1));
    CHECK(oncRPCXdrOpaque(&x, fixed, 5));
    CHECK(oncRPCXdrBytes(&x, &var, &vlen, 16));
    CHECK(oncRPCXdrString(&x, &s, 16));
    CHECK(oncRPCXdrString(&x, &null_s, 16));
    CHECK(bytes_eq(&x, exp, sizeof(exp)));
    oncRPCXdrDestroy(&x);

    oncRPCXdrInitDecode(&x, &A, exp, sizeof(exp));
    CHECK(oncRPCXdrOpaque(&x, back, 5) && memcmp(back, fixed, 5) == 0);
    CHECK(oncRPCXdrBytes(&x, &vback, &vbacklen, 16) && vbacklen == 3 &&
          memcmp(vback, "abc", 3) == 0);
    CHECK(oncRPCXdrString(&x, &sback, 16) && strcmp(sback, "hello") == 0);
    oncRPCXdrInitFree(&x, &A);
    CHECK(oncRPCXdrBytes(&x, &vback, &vbacklen, 16) && vback == NULL && vbacklen == 0);
    CHECK(oncRPCXdrString(&x, &sback, 16) && sback == NULL);

    /* bounds */
    {
        uint8_t buf[64];
        oncRPCXdrInitEncode(&x, buf, sizeof(buf));
        CHECK(!oncRPCXdrString(&x, &s, 4));          /* longer than max */
        oncRPCXdrInitDecode(&x, &A, exp + 16, 12);
        CHECK(!oncRPCXdrString(&x, &sback, 4));      /* decode bound */
        oncRPCXdrInitDecode(&x, &A, exp + 16, 7);
        CHECK(!oncRPCXdrString(&x, &sback, 64));     /* truncated input */
        CHECK(sback == NULL);
    }
    /* fixed buffer overflow */
    {
        uint8_t small[6];
        oncRPCXdrInitEncode(&x, small, sizeof(small));
        CHECK(!oncRPCXdrString(&x, &s, 16));
        CHECK(x.error);
    }
}

static int xdr_i32(oncRPCXdr *x, void *p)
{
    return oncRPCXdrInt32(x, (int32_t *)p);
}

typedef struct node {
    int32_t      v;
    struct node *next;
} node;

static int xdr_node(oncRPCXdr *x, void *p)
{
    node *n = (node *)p;
    return oncRPCXdrInt32(x, &n->v) &&
           oncRPCXdrPointer(x, (void **)&n->next, sizeof(node), xdr_node);
}

static void test_generic(void)
{
    oncRPCXdr x;
    int32_t arr[3] = { 7, 8, 9 }, vec[2] = { -1, -2 }, vecback[2];
    void *ap = arr, *back = NULL;
    uint32_t alen = 3, blen = 0;
    node n2 = { 2, NULL }, n1 = { 1, &n2 }, *list = &n1, *lback = NULL;
    uint8_t *data;
    size_t len;

    CHECK(oncRPCXdrInitEncodeDynamic(&x, &A, 0));
    CHECK(oncRPCXdrArray(&x, &ap, &alen, 10, sizeof(int32_t), xdr_i32));
    CHECK(oncRPCXdrVector(&x, vec, 2, sizeof(int32_t), xdr_i32));
    CHECK(oncRPCXdrPointer(&x, (void **)&list, sizeof(node), xdr_node));
    data = oncRPCXdrDetach(&x, &len);
    CHECK(data != NULL && len == 4 + 12 + 8 + 4 + 4 + 4 + 4 + 4);

    oncRPCXdrInitDecode(&x, &A, data, len);
    CHECK(oncRPCXdrArray(&x, &back, &blen, 10, sizeof(int32_t), xdr_i32));
    CHECK(blen == 3 && ((int32_t *)back)[2] == 9);
    CHECK(oncRPCXdrVector(&x, vecback, 2, sizeof(int32_t), xdr_i32) && vecback[1] == -2);
    CHECK(oncRPCXdrPointer(&x, (void **)&lback, sizeof(node), xdr_node));
    CHECK(lback != NULL && lback->v == 1 && lback->next != NULL && lback->next->v == 2 &&
          lback->next->next == NULL);
    oncRPCXdrFree(&A, xdr_node, lback);    /* frees the tail */
    oncRPCFree(&A, lback);
    oncRPCXdrInitFree(&x, &A);
    CHECK(oncRPCXdrArray(&x, &back, &blen, 10, sizeof(int32_t), xdr_i32) && back == NULL);

    /* array count larger than the bound or the input */
    oncRPCXdrInitDecode(&x, &A, data, len);
    CHECK(!oncRPCXdrArray(&x, &back, &blen, 2, sizeof(int32_t), xdr_i32));
    {
        static const uint8_t huge[] = { 0x7f, 0xff, 0xff, 0xff, 0, 0, 0, 1 };
        oncRPCXdrInitDecode(&x, &A, huge, sizeof(huge));
        CHECK(!oncRPCXdrArray(&x, &back, &blen, ONCRPC_XDR_UNBOUNDED, sizeof(int32_t),
                                xdr_i32));
        CHECK(back == NULL);
    }
    oncRPCFree(&A, data);
}

static void test_auth_sys(void)
{
    oncRPCAuthSysParams p, q;
    oncRPCXdr x;
    uint8_t buf[400];
    static const uint8_t exp[] = {
        0, 0, 0, 42,                      /* stamp */
        0, 0, 0, 4, 'h', 'o', 's', 't',   /* machinename */
        0, 0, 3, 0xe8,                    /* uid 1000 */
        0, 0, 0, 100,                     /* gid */
        0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 27
    };

    memset(&p, 0, sizeof(p));
    p.stamp = 42;
    memcpy(p.machinename, "host", sizeof("host"));
    p.uid = 1000;
    p.gid = 100;
    p.ngids = 2;
    p.gids[0] = 4;
    p.gids[1] = 27;
    oncRPCXdrInitEncode(&x, buf, sizeof(buf));
    CHECK(oncRPCXdrAuthSys(&x, &p));
    CHECK(bytes_eq(&x, exp, sizeof(exp)));
    memset(&q, 0, sizeof(q));
    oncRPCXdrInitDecode(&x, &A, exp, sizeof(exp));
    CHECK(oncRPCXdrAuthSys(&x, &q) && q.uid == 1000 && q.ngids == 2 && q.gids[1] == 27 &&
          strcmp(q.machinename, "host") == 0);
}

static void test_misc(void)
{
    CHECK(strcmp(oncRPCStrError(ONCRPC_OK), "success") == 0);
    CHECK(strcmp(oncRPCStrError(ONCRPC_ERR_TIMEDOUT), "timed out") == 0);
    CHECK(strcmp(oncRPCStrError(ONCRPC_ERR_CANCELLED), "cancelled") == 0);
}

int main(void)
{
    test_integers();
    test_small_and_bool();
    test_float();
    test_opaque_and_string();
    test_generic();
    test_auth_sys();
    test_misc();
    CHECK(g_live == 0);
    printf("test_xdr: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
