/*
 * test_gen.c - round-trips every kind of generated type through the
 * generated XDR routines: encode -> decode -> encode must give identical
 * bytes, decoded values must match, and freeing must release everything
 * (run under valgrind to confirm).
 */
/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alltypes.h"

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

#define T_CHECK(cond) do { g_count++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Encode obj with proc, decode into out (zeroed), re-encode and compare. */
static int roundtrip(oncRPCXdrProc proc, void *obj, void *out)
{
    oncRPCXdr a, b, d;
    int ok = 0;

    if (!oncRPCXdrInitEncodeDynamic(&a, &A, 64))
        return 0;
    if (!oncRPCXdrInitEncodeDynamic(&b, &A, 64)) {
        oncRPCXdrDestroy(&a);
        return 0;
    }
    if (proc(&a, obj)) {
        oncRPCXdrInitDecode(&d, &A, oncRPCXdrData(&a), oncRPCXdrLength(&a));
        if (proc(&d, out) && oncRPCXdrRemaining(&d) == 0 && proc(&b, out) &&
            oncRPCXdrLength(&a) == oncRPCXdrLength(&b) &&
            memcmp(oncRPCXdrData(&a), oncRPCXdrData(&b), oncRPCXdrLength(&a)) == 0)
            ok = 1;
        /* Truncated input must fail cleanly at every length. */
        if (ok) {
            size_t cut;
            for (cut = 0; cut < oncRPCXdrLength(&a); cut += 4) {
                oncRPCXdr t;
                union { long double ld; void *p; unsigned char b[4096]; } scratch;
                memset(&scratch, 0, sizeof(scratch));
                oncRPCXdrInitDecode(&t, &A, oncRPCXdrData(&a), cut);
                if (proc(&t, &scratch))
                    ok = 0;
                oncRPCXdrFree(&A, proc, &scratch);
            }
        }
    }
    oncRPCXdrDestroy(&a);
    oncRPCXdrDestroy(&b);
    return ok;
}

static int w_everything(oncRPCXdr *x, void *p) { return oncRPCXdrEverything(x, (everything *)p); }
static int w_list(oncRPCXdr *x, void *p) { return oncRPCXdrList(x, (list *)p); }
static int w_flag(oncRPCXdr *x, void *p) { return oncRPCXdrFlag(x, (flag *)p); }
static int w_args(oncRPCXdr *x, void *p) { return oncRPCXdrListsV1Args(x, (oncRPCListsV1Args *)p); }

static void test_everything(void)
{
    everything e, out;
    point pts[3] = { { 1, 2 }, { 3, 4 }, { 5, 6 } }, single = { 9.5f, -9.5f }, other = { 7, 8 };
    uint8_t vr[3] = { 0xaa, 0xbb, 0xcc }, bl[6] = { 1, 2, 3, 4, 5, 6 };
    int32_t nums[2] = { -7, 7 };
    label labs[2];
    char l1[] = "first", l2[] = "second", tag[] = "tagged", nm[] = "everything", why[] = "because";

    memset(&e, 0, sizeof(e));
    e.i = -1;
    e.u = 0xffffffffu;
    e.h = -1234567890123ll;
    e.uh = 0xfedcba9876543210ull;
    e.f = 0.25f;
    e.d = 1e300;
    e.b = 1;
    e.c = -100;
    e.uc = 200;
    e.s = -30000;
    e.us = 60000;
    e.sh = TRIANGLE;
    e.fixed_ints[0] = 1;
    e.fixed_ints[3] = 4;
    e.pts.pts_len = 3;
    e.pts.pts_val = pts;
    e.some_pts.some_pts_len = 0;
    memcpy(e.raw, "abcde", 5);
    e.var_raw.var_raw_len = 3;
    e.var_raw.var_raw_val = vr;
    e.name = nm;
    e.tag = tag;
    memcpy(e.fh, "HANDLE!!", 8);
    e.data.blob_len = 6;
    e.data.blob_val = bl;
    e.numbers.ints_len = 2;
    e.numbers.ints_val = nums;
    e.w[0] = 1;
    e.w[2] = 3;
    e.maybe = &single;
    e.maybe2 = &other;
    labs[0] = l1;
    labs[1] = l2;
    e.labels.labels_len = 2;
    e.labels.labels_val = labs;
    e.nested.depth = 3;
    e.nested.why = why;

    memset(&out, 0, sizeof(out));
    T_CHECK(roundtrip(w_everything, &e, &out));
    T_CHECK(out.i == -1 && out.u == 0xffffffffu && out.h == -1234567890123ll);
    T_CHECK(out.uh == 0xfedcba9876543210ull && out.f == 0.25f && out.d == 1e300);
    T_CHECK(out.b == 1 && out.c == -100 && out.uc == 200 && out.s == -30000 && out.us == 60000);
    T_CHECK(out.sh == TRIANGLE && out.fixed_ints[3] == 4);
    T_CHECK(out.pts.pts_len == 3 && out.pts.pts_val[2].y == 6);
    T_CHECK(out.some_pts.some_pts_len == 0 && out.some_pts.some_pts_val == NULL);
    T_CHECK(memcmp(out.raw, "abcde", 5) == 0);
    T_CHECK(out.var_raw.var_raw_len == 3 && out.var_raw.var_raw_val[2] == 0xcc);
    T_CHECK(strcmp(out.name, "everything") == 0 && strcmp(out.tag, "tagged") == 0);
    T_CHECK(memcmp(out.fh, "HANDLE!!", 8) == 0);
    T_CHECK(out.data.blob_len == 6 && out.data.blob_val[5] == 6);
    T_CHECK(out.numbers.ints_len == 2 && out.numbers.ints_val[0] == -7);
    T_CHECK(out.w[2] == 3);
    T_CHECK(out.maybe != NULL && out.maybe->x == 9.5f);
    T_CHECK(out.maybe2 != NULL && out.maybe2->y == 8);
    T_CHECK(out.labels.labels_len == 2 && strcmp(out.labels.labels_val[1], "second") == 0);
    T_CHECK(out.nested.depth == 3 && strcmp(out.nested.why, "because") == 0);
    oncRPCXdrFreeEverything(&A, &out);
    T_CHECK(out.name == NULL && out.pts.pts_val == NULL && out.maybe == NULL);

    /* Bounds enforced on encode: numbers is ints<SMALL> */
    {
        int32_t many[5] = { 0 };
        oncRPCXdr x;
        e.numbers.ints_len = 5;
        e.numbers.ints_val = many;
        T_CHECK(oncRPCXdrInitEncodeDynamic(&x, &A, 64));
        T_CHECK(!oncRPCXdrEverything(&x, &e));
        oncRPCXdrDestroy(&x);
        e.numbers.ints_len = 2;
        e.numbers.ints_val = nums;
    }
}

static void test_unions_and_lists(void)
{
    point corners[3] = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
    list n3, n2, n1, out;
    flag f, fout;
    oncRPCListsV1Args args, aout;

    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    memset(&n3, 0, sizeof(n3));
    n1.item.kind = CIRCLE;
    n1.item.shape_info_u.radius = 2.5;
    n1.next = &n2;
    n2.item.kind = TRIANGLE;
    n2.item.shape_info_u.corners.corners_len = 3;
    n2.item.shape_info_u.corners.corners_val = corners;
    n2.next = &n3;
    n3.item.kind = (shape)42;   /* default: void arm */
    n3.next = NULL;

    memset(&out, 0, sizeof(out));
    T_CHECK(roundtrip(w_list, &n1, &out));
    T_CHECK(out.item.kind == CIRCLE && out.item.shape_info_u.radius == 2.5);
    T_CHECK(out.next != NULL && out.next->item.kind == TRIANGLE &&
            out.next->item.shape_info_u.corners.corners_len == 3 &&
            out.next->item.shape_info_u.corners.corners_val[1].x == 1);
    T_CHECK(out.next->next != NULL && out.next->next->item.kind == 42 &&
            out.next->next->next == NULL);
    oncRPCXdrFreeList(&A, &out);

    /* bool discriminant with TRUE/FALSE cases */
    f.set = 1;
    f.flag_u.value = 77;
    memset(&fout, 0, sizeof(fout));
    T_CHECK(roundtrip(w_flag, &f, &fout) && fout.set == 1 && fout.flag_u.value == 77);
    f.set = 0;
    memset(&fout, 0, sizeof(fout));
    T_CHECK(roundtrip(w_flag, &f, &fout) && fout.set == 0);

    /* multi-argument struct generated for LISTS(list, shape_info, string) */
    memset(&args, 0, sizeof(args));
    args.arg1 = n1;
    args.arg2.kind = SQUARE;
    args.arg3 = "third";
    memset(&aout, 0, sizeof(aout));
    T_CHECK(roundtrip(w_args, &args, &aout));
    T_CHECK(aout.arg2.kind == SQUARE && strcmp(aout.arg3, "third") == 0 &&
            aout.arg1.next != NULL);
    oncRPCXdrFreeListsV1Args(&A, &aout);
}

static void test_wire_format(void)
{
    /* point {1.0, -2.0} must be two IEEE floats, big-endian. */
    static const uint8_t exp[] = { 0x3f, 0x80, 0, 0, 0xc0, 0, 0, 0 };
    point p = { 1.0f, -2.0f };
    uint8_t buf[8];
    oncRPCXdr x;
    oncRPCXdrInitEncode(&x, buf, sizeof(buf));
    T_CHECK(oncRPCXdrPoint(&x, &p) && memcmp(buf, exp, 8) == 0);

    /* enum value outside the declaration still round-trips as an int */
    {
        shape s = SQUARE;
        static const uint8_t two[] = { 0, 0, 0, 2 };
        oncRPCXdrInitEncode(&x, buf, sizeof(buf));
        T_CHECK(oncRPCXdrShape(&x, &s) && memcmp(buf, two, 4) == 0);
    }
}

int main(void)
{
    test_everything();
    test_unions_and_lists();
    test_wire_format();
    g_count++;
    if (g_live != 0) {
        g_fail++;
        fprintf(stderr, "leaked %ld blocks\n", g_live);
    }
    printf("test_gen: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
