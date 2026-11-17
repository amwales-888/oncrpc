/*
 * xdr.c - XDR (RFC 4506) encoding and decoding over memory buffers.
 *
 * All quantities are big-endian and padded to 4-byte boundaries. Floating
 * point values are assumed to be IEEE 754 single/double precision in host
 * byte order, which holds for every platform this library targets.
 */
#include "xdr.h"

#include <string.h>

typedef char oncRPCCheckFloatSize[(sizeof(float) == 4) ? 1 : -1];
typedef char oncRPCCheckDoubleSize[(sizeof(double) == 8) ? 1 : -1];

/* ---- Memory ------------------------------------------------------------ */

void *oncRPCRealloc(const oncRPCAllocator *a, void *ptr, size_t size)
{
    if (a == NULL || a->realloc == NULL)
        return NULL;
    return (a->realloc)(a->ctx, ptr, size ? size : 1);
}

void oncRPCFree(const oncRPCAllocator *a, void *ptr)
{
    if (ptr != NULL && a != NULL && a->free != NULL)
        (a->free)(a->ctx, ptr);
}

void *oncRPCAlloc(const oncRPCAllocator *a, size_t size)
{
    void *p;
    if (size == 0)
        size = 1;
    p = oncRPCRealloc(a, NULL, size);
    if (p != NULL)
        memset(p, 0, size);
    return p;
}

void *oncRPCAllocArray(const oncRPCAllocator *a, size_t count, size_t size)
{
    if (size != 0 && count > ((size_t)-1) / size)
        return NULL;
    return oncRPCAlloc(a, count * size);
}

char *oncRPCStrdup(const oncRPCAllocator *a, const char *s)
{
    size_t n;
    char *d;
    if (s == NULL)
        return NULL;
    n = strlen(s) + 1;
    d = (char *)oncRPCRealloc(a, NULL, n);
    if (d != NULL)
        memcpy(d, s, n);
    return d;
}

/* ---- Streams ----------------------------------------------------------- */

void oncRPCXdrInitEncode(oncRPCXdr *xdrs, uint8_t *buf, size_t cap)
{
    memset(xdrs, 0, sizeof(*xdrs));
    xdrs->op = ONCRPC_XDR_ENCODE;
    xdrs->buf = buf;
    xdrs->cap = buf ? cap : 0;
}

int oncRPCXdrInitEncodeDynamic(oncRPCXdr *xdrs, const oncRPCAllocator *a, size_t initial)
{
    memset(xdrs, 0, sizeof(*xdrs));
    xdrs->op = ONCRPC_XDR_ENCODE;
    xdrs->alloc = a;
    xdrs->dynamic = 1;
    if (initial < 64)
        initial = 64;
    xdrs->buf = (uint8_t *)oncRPCRealloc(a, NULL, initial);
    if (xdrs->buf == NULL) {
        xdrs->error = 1;
        return 0;
    }
    xdrs->cap = initial;
    return 1;
}

void oncRPCXdrInitDecode(oncRPCXdr *xdrs, const oncRPCAllocator *a, const uint8_t *buf,
                         size_t len)
{
    memset(xdrs, 0, sizeof(*xdrs));
    xdrs->op = ONCRPC_XDR_DECODE;
    xdrs->alloc = a;
    /* The buffer is never written in decode mode. */
    xdrs->buf = (uint8_t *)buf;
    xdrs->len = buf ? len : 0;
}

void oncRPCXdrInitFree(oncRPCXdr *xdrs, const oncRPCAllocator *a)
{
    memset(xdrs, 0, sizeof(*xdrs));
    xdrs->op = ONCRPC_XDR_FREE;
    xdrs->alloc = a;
}

void oncRPCXdrDestroy(oncRPCXdr *xdrs)
{
    if (xdrs == NULL)
        return;
    if (xdrs->dynamic)
        oncRPCFree(xdrs->alloc, xdrs->buf);
    xdrs->buf = NULL;
    xdrs->cap = xdrs->len = xdrs->pos = 0;
    xdrs->dynamic = 0;
}

void oncRPCXdrReset(oncRPCXdr *xdrs)
{
    xdrs->pos = 0;
    if (xdrs->op == ONCRPC_XDR_ENCODE)
        xdrs->len = 0;
    xdrs->error = 0;
}

size_t oncRPCXdrGetPos(const oncRPCXdr *xdrs)
{
    return xdrs->pos;
}

int oncRPCXdrSetPos(oncRPCXdr *xdrs, size_t pos)
{
    if (pos > xdrs->len)
        return 0;
    xdrs->pos = pos;
    return 1;
}

const uint8_t *oncRPCXdrData(const oncRPCXdr *xdrs)
{
    return xdrs->buf;
}

size_t oncRPCXdrLength(const oncRPCXdr *xdrs)
{
    return xdrs->len;
}

size_t oncRPCXdrRemaining(const oncRPCXdr *xdrs)
{
    switch (xdrs->op) {
    case ONCRPC_XDR_DECODE:
        return xdrs->len - xdrs->pos;
    case ONCRPC_XDR_ENCODE:
        return xdrs->dynamic ? (size_t)-1 : xdrs->cap - xdrs->pos;
    default:
        return 0;
    }
}

uint8_t *oncRPCXdrDetach(oncRPCXdr *xdrs, size_t *len)
{
    uint8_t *b;
    if (!xdrs->dynamic)
        return NULL;
    b = xdrs->buf;
    if (len != NULL)
        *len = xdrs->len;
    xdrs->buf = NULL;
    xdrs->dynamic = 0;
    xdrs->cap = xdrs->len = xdrs->pos = 0;
    return b;
}

void oncRPCXdrFree(const oncRPCAllocator *a, oncRPCXdrProc proc, void *obj)
{
    oncRPCXdr x;
    if (proc == NULL || obj == NULL)
        return;
    oncRPCXdrInitFree(&x, a);
    (void)proc(&x, obj);
}

void oncRPCXdrMemFree(oncRPCXdr *xdrs, void *ptr)
{
    oncRPCFree(xdrs->alloc, ptr);
}

/* ---- Raw byte movement --------------------------------------------------- */

static int fail(oncRPCXdr *xdrs)
{
    xdrs->error = 1;
    return 0;
}

static int reserve(oncRPCXdr *xdrs, size_t n)
{
    size_t need, newcap;
    uint8_t *p;

    if (xdrs->error)
        return 0;
    if (n > (size_t)-1 - xdrs->pos)
        return fail(xdrs);
    need = xdrs->pos + n;
    if (need <= xdrs->cap)
        return 1;
    if (!xdrs->dynamic)
        return fail(xdrs);
    newcap = xdrs->cap ? xdrs->cap : 64;
    while (newcap < need) {
        if (newcap > ((size_t)-1) / 2) {
            newcap = need;
            break;
        }
        newcap *= 2;
    }
    p = (uint8_t *)oncRPCRealloc(xdrs->alloc, xdrs->buf, newcap);
    if (p == NULL)
        return fail(xdrs);
    xdrs->buf = p;
    xdrs->cap = newcap;
    return 1;
}

static int put(oncRPCXdr *xdrs, const void *data, size_t n)
{
    if (!reserve(xdrs, n))
        return 0;
    if (n > 0) {
        if (data != NULL)
            memcpy(xdrs->buf + xdrs->pos, data, n);
        else
            memset(xdrs->buf + xdrs->pos, 0, n);
    }
    xdrs->pos += n;
    if (xdrs->pos > xdrs->len)
        xdrs->len = xdrs->pos;
    return 1;
}

static int get(oncRPCXdr *xdrs, void *data, size_t n)
{
    if (xdrs->error)
        return 0;
    if (n > xdrs->len - xdrs->pos)
        return fail(xdrs);
    if (n > 0 && data != NULL)
        memcpy(data, xdrs->buf + xdrs->pos, n);
    xdrs->pos += n;
    return 1;
}

static uint32_t pad_of(uint32_t n)
{
    return (ONCRPC_XDR_UNIT - (n & 3u)) & 3u;
}

static int32_t u2s32(uint32_t u)
{
    if (u <= 0x7fffffffu)
        return (int32_t)u;
    return (int32_t)(-(int32_t)(~u) - 1);
}

static int64_t u2s64(uint64_t u)
{
    if (u <= 0x7fffffffffffffffull)
        return (int64_t)u;
    return (int64_t)(-(int64_t)(~u) - 1);
}

static void *xalloc(oncRPCXdr *xdrs, size_t n)
{
    void *p = oncRPCAlloc(xdrs->alloc, n);
    if (p == NULL)
        xdrs->error = 1;
    return p;
}

/* ---- Primitives -------------------------------------------------------- */

int oncRPCXdrVoid(oncRPCXdr *xdrs, void *unused)
{
    (void)xdrs;
    (void)unused;
    return 1;
}

int oncRPCXdrUint32(oncRPCXdr *xdrs, uint32_t *v)
{
    uint8_t b[4];
    switch (xdrs->op) {
    case ONCRPC_XDR_ENCODE:
        b[0] = (uint8_t)(*v >> 24);
        b[1] = (uint8_t)(*v >> 16);
        b[2] = (uint8_t)(*v >> 8);
        b[3] = (uint8_t)(*v);
        return put(xdrs, b, 4);
    case ONCRPC_XDR_DECODE:
        if (!get(xdrs, b, 4))
            return 0;
        *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
             ((uint32_t)b[2] << 8) | (uint32_t)b[3];
        return 1;
    case ONCRPC_XDR_FREE:
        return 1;
    }
    return 0;
}

int oncRPCXdrInt32(oncRPCXdr *xdrs, int32_t *v)
{
    uint32_t u = 0;
    if (xdrs->op == ONCRPC_XDR_ENCODE)
        u = (uint32_t)*v;
    if (!oncRPCXdrUint32(xdrs, &u))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        *v = u2s32(u);
    return 1;
}

int oncRPCXdrUint64(oncRPCXdr *xdrs, uint64_t *v)
{
    uint32_t hi = 0, lo = 0;
    if (xdrs->op == ONCRPC_XDR_FREE)
        return 1;
    if (xdrs->op == ONCRPC_XDR_ENCODE) {
        hi = (uint32_t)(*v >> 32);
        lo = (uint32_t)(*v & 0xffffffffu);
    }
    if (!oncRPCXdrUint32(xdrs, &hi) || !oncRPCXdrUint32(xdrs, &lo))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        *v = ((uint64_t)hi << 32) | lo;
    return 1;
}

int oncRPCXdrInt64(oncRPCXdr *xdrs, int64_t *v)
{
    uint64_t u = 0;
    if (xdrs->op == ONCRPC_XDR_ENCODE)
        u = (uint64_t)*v;
    if (!oncRPCXdrUint64(xdrs, &u))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        *v = u2s64(u);
    return 1;
}

int oncRPCXdrFloat(oncRPCXdr *xdrs, float *v)
{
    uint32_t u = 0;
    if (xdrs->op == ONCRPC_XDR_ENCODE)
        memcpy(&u, v, 4);
    if (!oncRPCXdrUint32(xdrs, &u))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        memcpy(v, &u, 4);
    return 1;
}

int oncRPCXdrDouble(oncRPCXdr *xdrs, double *v)
{
    uint64_t u = 0;
    if (xdrs->op == ONCRPC_XDR_ENCODE)
        memcpy(&u, v, 8);
    if (!oncRPCXdrUint64(xdrs, &u))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        memcpy(v, &u, 8);
    return 1;
}

int oncRPCXdrBool(oncRPCXdr *xdrs, oncRPCBool *v)
{
    uint32_t u = 0;
    if (xdrs->op == ONCRPC_XDR_ENCODE)
        u = *v ? 1u : 0u;
    if (!oncRPCXdrUint32(xdrs, &u))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE) {
        if (u > 1)
            return fail(xdrs);
        *v = (oncRPCBool)u;
    }
    return 1;
}

int oncRPCXdrEnum(oncRPCXdr *xdrs, int32_t *v)
{
    return oncRPCXdrInt32(xdrs, v);
}

static int small_int(oncRPCXdr *xdrs, int32_t *t, int32_t lo, int32_t hi)
{
    if (!oncRPCXdrInt32(xdrs, t))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE && (*t < lo || *t > hi))
        return fail(xdrs);
    return 1;
}

int oncRPCXdrInt8(oncRPCXdr *xdrs, int8_t *v)
{
    int32_t t = (xdrs->op == ONCRPC_XDR_ENCODE) ? *v : 0;
    if (!small_int(xdrs, &t, -128, 127))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        *v = (int8_t)t;
    return 1;
}

int oncRPCXdrUint8(oncRPCXdr *xdrs, uint8_t *v)
{
    int32_t t = (xdrs->op == ONCRPC_XDR_ENCODE) ? *v : 0;
    if (!small_int(xdrs, &t, 0, 255))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        *v = (uint8_t)t;
    return 1;
}

int oncRPCXdrInt16(oncRPCXdr *xdrs, int16_t *v)
{
    int32_t t = (xdrs->op == ONCRPC_XDR_ENCODE) ? *v : 0;
    if (!small_int(xdrs, &t, -32768, 32767))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        *v = (int16_t)t;
    return 1;
}

int oncRPCXdrUint16(oncRPCXdr *xdrs, uint16_t *v)
{
    int32_t t = (xdrs->op == ONCRPC_XDR_ENCODE) ? *v : 0;
    if (!small_int(xdrs, &t, 0, 65535))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        *v = (uint16_t)t;
    return 1;
}

/* ---- Opaque data and strings ------------------------------------------- */

int oncRPCXdrOpaque(oncRPCXdr *xdrs, uint8_t *data, uint32_t len)
{
    uint32_t pad = pad_of(len);
    switch (xdrs->op) {
    case ONCRPC_XDR_ENCODE:
        if (len > 0 && data == NULL)
            return fail(xdrs);
        return put(xdrs, data, len) && put(xdrs, NULL, pad);
    case ONCRPC_XDR_DECODE:
        if (len > 0 && data == NULL)
            return fail(xdrs);
        return get(xdrs, data, len) && get(xdrs, NULL, pad);
    case ONCRPC_XDR_FREE:
        return 1;
    }
    return 0;
}

int oncRPCXdrBytes(oncRPCXdr *xdrs, uint8_t **data, uint32_t *len, uint32_t max)
{
    uint32_t n;
    uint8_t *p;

    switch (xdrs->op) {
    case ONCRPC_XDR_ENCODE:
        if (*len > max || (*len > 0 && *data == NULL))
            return fail(xdrs);
        return oncRPCXdrUint32(xdrs, len) && oncRPCXdrOpaque(xdrs, *data, *len);
    case ONCRPC_XDR_DECODE:
        if (!oncRPCXdrUint32(xdrs, &n))
            return 0;
        if (n > max || n > oncRPCXdrRemaining(xdrs))
            return fail(xdrs);
        p = (uint8_t *)xalloc(xdrs, n);
        if (p == NULL)
            return 0;
        if (!oncRPCXdrOpaque(xdrs, p, n)) {
            oncRPCFree(xdrs->alloc, p);
            return 0;
        }
        *data = p;
        *len = n;
        return 1;
    case ONCRPC_XDR_FREE:
        oncRPCFree(xdrs->alloc, *data);
        *data = NULL;
        *len = 0;
        return 1;
    }
    return 0;
}

int oncRPCXdrString(oncRPCXdr *xdrs, char **s, uint32_t max)
{
    static char empty[1] = { 0 };
    size_t sl;
    uint32_t n;
    char *p;

    switch (xdrs->op) {
    case ONCRPC_XDR_ENCODE:
        sl = (*s != NULL) ? strlen(*s) : 0;
        if (sl > max || sl > UINT32_MAX)
            return fail(xdrs);
        n = (uint32_t)sl;
        return oncRPCXdrUint32(xdrs, &n) &&
               oncRPCXdrOpaque(xdrs, (uint8_t *)(*s != NULL ? *s : empty), n);
    case ONCRPC_XDR_DECODE:
        if (!oncRPCXdrUint32(xdrs, &n))
            return 0;
        if (n > max || n > oncRPCXdrRemaining(xdrs))
            return fail(xdrs);
        p = (char *)xalloc(xdrs, (size_t)n + 1);
        if (p == NULL)
            return 0;
        if (!oncRPCXdrOpaque(xdrs, (uint8_t *)p, n)) {
            oncRPCFree(xdrs->alloc, p);
            return 0;
        }
        p[n] = '\0';
        *s = p;
        return 1;
    case ONCRPC_XDR_FREE:
        oncRPCFree(xdrs->alloc, *s);
        *s = NULL;
        return 1;
    }
    return 0;
}

/* ---- Arrays and pointers ------------------------------------------------ */

int oncRPCXdrArrayLen(oncRPCXdr *xdrs, uint32_t *len, uint32_t max)
{
    uint32_t n;
    switch (xdrs->op) {
    case ONCRPC_XDR_ENCODE:
        if (*len > max)
            return fail(xdrs);
        return oncRPCXdrUint32(xdrs, len);
    case ONCRPC_XDR_DECODE:
        if (!oncRPCXdrUint32(xdrs, &n))
            return 0;
        /* Every XDR item occupies at least one byte on the wire, so a
         * count larger than the remaining input is necessarily bogus. */
        if (n > max || n > oncRPCXdrRemaining(xdrs))
            return fail(xdrs);
        *len = n;
        return 1;
    case ONCRPC_XDR_FREE:
        return 1;
    }
    return 0;
}

void *oncRPCXdrArrayAlloc(oncRPCXdr *xdrs, uint32_t count, size_t elsize)
{
    void *p = oncRPCAllocArray(xdrs->alloc, count, elsize);
    if (p == NULL)
        xdrs->error = 1;
    return p;
}

int oncRPCXdrArray(oncRPCXdr *xdrs, void **arr, uint32_t *len, uint32_t max, size_t elsize,
                   oncRPCXdrProc elproc)
{
    uint32_t i;
    uint8_t *base;

    if (!oncRPCXdrArrayLen(xdrs, len, max))
        return 0;
    if (xdrs->op == ONCRPC_XDR_ENCODE && *len > 0 && *arr == NULL)
        return fail(xdrs);
    if (xdrs->op == ONCRPC_XDR_DECODE) {
        *arr = NULL;
        if (*len > 0) {
            *arr = oncRPCXdrArrayAlloc(xdrs, *len, elsize);
            if (*arr == NULL) {
                *len = 0;
                return 0;
            }
        }
    }
    base = (uint8_t *)*arr;
    if (base != NULL) {
        for (i = 0; i < *len; i++) {
            if (!elproc(xdrs, base + (size_t)i * elsize) && xdrs->op != ONCRPC_XDR_FREE)
                return 0;
        }
    }
    if (xdrs->op == ONCRPC_XDR_FREE) {
        oncRPCFree(xdrs->alloc, *arr);
        *arr = NULL;
        *len = 0;
    }
    return 1;
}

int oncRPCXdrVector(oncRPCXdr *xdrs, void *arr, uint32_t count, size_t elsize,
                    oncRPCXdrProc elproc)
{
    uint32_t i;
    uint8_t *base = (uint8_t *)arr;
    for (i = 0; i < count; i++) {
        if (!elproc(xdrs, base + (size_t)i * elsize) && xdrs->op != ONCRPC_XDR_FREE)
            return 0;
    }
    return 1;
}

int oncRPCXdrPointer(oncRPCXdr *xdrs, void **pp, size_t size, oncRPCXdrProc proc)
{
    oncRPCBool more = (*pp != NULL);

    if (xdrs->op == ONCRPC_XDR_FREE) {
        if (*pp != NULL) {
            (void)proc(xdrs, *pp);
            oncRPCFree(xdrs->alloc, *pp);
            *pp = NULL;
        }
        return 1;
    }
    if (!oncRPCXdrBool(xdrs, &more))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE) {
        *pp = NULL;
        if (!more)
            return 1;
        *pp = oncRPCXdrArrayAlloc(xdrs, 1, size);
        if (*pp == NULL)
            return 0;
    }
    if (!more)
        return 1;
    return proc(xdrs, *pp);
}
