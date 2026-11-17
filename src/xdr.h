/*
 * xdr.h - External Data Representation (XDR, RFC 4506) for liboncrpc.
 *
 * XDR streams operate on memory buffers and never block. A stream is in
 * one of three modes:
 *
 *   ONCRPC_XDR_ENCODE  native value  -> network bytes
 *   ONCRPC_XDR_DECODE  network bytes -> native value (allocates as needed)
 *   ONCRPC_XDR_FREE    releases memory allocated by a previous decode
 *
 * Every XDR routine (oncRPCXdr* and the xdr_* routines produced by
 * oncrpcgen) returns non-zero on success and 0 on failure, and works in all
 * three modes, so a single routine describes a type completely.
 *
 * liboncrpc never calls malloc/realloc/free itself. All memory comes from an
 * oncRPCAllocator supplied by the application.
 */
#ifndef ONCRPC_XDR_H
#define ONCRPC_XDR_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(ONCRPC_SHARED)
#  if defined(ONCRPC_BUILDING)
#    define ONCRPC_API __declspec(dllexport)
#  else
#    define ONCRPC_API __declspec(dllimport)
#  endif
#else
#  define ONCRPC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Size of the basic XDR block; every item is padded to a multiple of it. */
#define ONCRPC_XDR_UNIT       4u
/* Maximum length for unbounded strings, opaques and arrays (<>). */
#define ONCRPC_XDR_UNBOUNDED  UINT32_MAX

/*
 * Memory allocator supplied by the application.
 *
 *   realloc(ctx, ptr, size)  like C realloc: ptr may be NULL; size is never
 *                            0. Returns NULL on failure (ptr stays valid).
 *   free(ctx, ptr)           releases ptr; never called with NULL.
 *   ctx                      passed unchanged to both functions.
 *
 * The structure must stay valid, and unchanged, for as long as anything
 * created with it exists. The library invokes the members as
 * (a->realloc)(...) so that debug CRTs defining realloc/free as macros do
 * not interfere.
 */
typedef struct oncRPCAllocator {
    void *(*realloc)(void *ctx, void *ptr, size_t size);
    void  (*free)(void *ctx, void *ptr);
    void   *ctx;
} oncRPCAllocator;

/* XDR boolean: encoded as an int restricted to 0 and 1. */
typedef int32_t oncRPCBool;
#define ONCRPC_TRUE  1
#define ONCRPC_FALSE 0

typedef enum oncRPCXdrOp {
    ONCRPC_XDR_ENCODE = 0,
    ONCRPC_XDR_DECODE = 1,
    ONCRPC_XDR_FREE   = 2
} oncRPCXdrOp;

/* XDR stream. Treat the fields as read-only; use the functions below. */
typedef struct oncRPCXdr {
    oncRPCXdrOp            op;
    const oncRPCAllocator *alloc;    /* decode/free/dynamic encode */
    uint8_t               *buf;
    size_t                 len;      /* encode: bytes produced; decode: bytes available */
    size_t                 cap;      /* encode: capacity of buf */
    size_t                 pos;      /* current offset */
    int                    dynamic;  /* encode: buffer owned, grows on demand */
    int                    error;    /* set once any operation failed */
} oncRPCXdr;

/* Generic XDR routine, used wherever a routine is passed as a value. */
typedef int (*oncRPCXdrProc)(oncRPCXdr *xdrs, void *obj);

/* ---- Memory helpers (all go through the allocator) ------------------- */

/* Zero-initialised block of size bytes (size 0 is treated as 1). */
ONCRPC_API void *oncRPCAlloc(const oncRPCAllocator *a, size_t size);
/* Zero-initialised array; fails on overflow. */
ONCRPC_API void *oncRPCAllocArray(const oncRPCAllocator *a, size_t count, size_t size);
ONCRPC_API void *oncRPCRealloc(const oncRPCAllocator *a, void *ptr, size_t size);
/* NULL is ignored. */
ONCRPC_API void  oncRPCFree(const oncRPCAllocator *a, void *ptr);
ONCRPC_API char *oncRPCStrdup(const oncRPCAllocator *a, const char *s);

/* ---- Stream management ----------------------------------------------- */

/* Encode into a caller-supplied fixed buffer (no allocator needed). */
ONCRPC_API void   oncRPCXdrInitEncode(oncRPCXdr *xdrs, uint8_t *buf, size_t cap);
/* Encode into a buffer that grows as needed. Returns non-zero on success.
 * Release with oncRPCXdrDestroy() or take the buffer with oncRPCXdrDetach(). */
ONCRPC_API int    oncRPCXdrInitEncodeDynamic(oncRPCXdr *xdrs, const oncRPCAllocator *a,
                                             size_t initial);
/* Decode from buf (not copied). a is used for variable-size data and may be
 * NULL when only fixed-size data is decoded. */
ONCRPC_API void   oncRPCXdrInitDecode(oncRPCXdr *xdrs, const oncRPCAllocator *a,
                                      const uint8_t *buf, size_t len);
/* A stream whose only purpose is to free decoded data. */
ONCRPC_API void   oncRPCXdrInitFree(oncRPCXdr *xdrs, const oncRPCAllocator *a);
/* Release a dynamic encode buffer. Safe on any stream. */
ONCRPC_API void   oncRPCXdrDestroy(oncRPCXdr *xdrs);
/* Rewind to the start (encode: discards produced bytes) and clear error. */
ONCRPC_API void   oncRPCXdrReset(oncRPCXdr *xdrs);
ONCRPC_API size_t oncRPCXdrGetPos(const oncRPCXdr *xdrs);
ONCRPC_API int    oncRPCXdrSetPos(oncRPCXdr *xdrs, size_t pos);
ONCRPC_API const uint8_t *oncRPCXdrData(const oncRPCXdr *xdrs);
ONCRPC_API size_t oncRPCXdrLength(const oncRPCXdr *xdrs);
/* Bytes left to decode (decode) or room left (fixed encode). */
ONCRPC_API size_t oncRPCXdrRemaining(const oncRPCXdr *xdrs);
/* Take ownership of a dynamic encode buffer (release with oncRPCFree). */
ONCRPC_API uint8_t *oncRPCXdrDetach(oncRPCXdr *xdrs, size_t *len);

/* Release everything a decode allocated inside obj, using routine proc. */
ONCRPC_API void   oncRPCXdrFree(const oncRPCAllocator *a, oncRPCXdrProc proc, void *obj);
/* Release a block through the stream's allocator (used by generated code). */
ONCRPC_API void   oncRPCXdrMemFree(oncRPCXdr *xdrs, void *ptr);

/* ---- Primitive types -------------------------------------------------- */
ONCRPC_API int oncRPCXdrVoid(oncRPCXdr *xdrs, void *unused);
ONCRPC_API int oncRPCXdrInt32(oncRPCXdr *xdrs, int32_t *v);
ONCRPC_API int oncRPCXdrUint32(oncRPCXdr *xdrs, uint32_t *v);
ONCRPC_API int oncRPCXdrInt64(oncRPCXdr *xdrs, int64_t *v);      /* hyper */
ONCRPC_API int oncRPCXdrUint64(oncRPCXdr *xdrs, uint64_t *v);    /* unsigned hyper */
ONCRPC_API int oncRPCXdrFloat(oncRPCXdr *xdrs, float *v);
ONCRPC_API int oncRPCXdrDouble(oncRPCXdr *xdrs, double *v);
ONCRPC_API int oncRPCXdrBool(oncRPCXdr *xdrs, oncRPCBool *v);
ONCRPC_API int oncRPCXdrEnum(oncRPCXdr *xdrs, int32_t *v);
/* Small integers travel as 32-bit XDR ints; decode checks the range. */
ONCRPC_API int oncRPCXdrInt8(oncRPCXdr *xdrs, int8_t *v);
ONCRPC_API int oncRPCXdrUint8(oncRPCXdr *xdrs, uint8_t *v);
ONCRPC_API int oncRPCXdrInt16(oncRPCXdr *xdrs, int16_t *v);
ONCRPC_API int oncRPCXdrUint16(oncRPCXdr *xdrs, uint16_t *v);

/* Fixed-length opaque data: opaque x[len]. */
ONCRPC_API int oncRPCXdrOpaque(oncRPCXdr *xdrs, uint8_t *data, uint32_t len);
/* Variable-length opaque data: opaque x<max>. Decode allocates *data. */
ONCRPC_API int oncRPCXdrBytes(oncRPCXdr *xdrs, uint8_t **data, uint32_t *len, uint32_t max);
/* String: string x<max>. Decode allocates a NUL-terminated *s.
 * Encoding a NULL pointer produces the empty string. */
ONCRPC_API int oncRPCXdrString(oncRPCXdr *xdrs, char **s, uint32_t max);

/* ---- Arrays and optional data ------------------------------------------ */

/* Building blocks used by generated code: encode/decode an array length
 * with a bound check, and allocate zeroed element storage while decoding. */
ONCRPC_API int   oncRPCXdrArrayLen(oncRPCXdr *xdrs, uint32_t *len, uint32_t max);
ONCRPC_API void *oncRPCXdrArrayAlloc(oncRPCXdr *xdrs, uint32_t count, size_t elsize);

/* Generic helpers for hand-written XDR routines. */
ONCRPC_API int oncRPCXdrArray(oncRPCXdr *xdrs, void **arr, uint32_t *len, uint32_t max,
                              size_t elsize, oncRPCXdrProc elproc);
ONCRPC_API int oncRPCXdrVector(oncRPCXdr *xdrs, void *arr, uint32_t count, size_t elsize,
                               oncRPCXdrProc elproc);
ONCRPC_API int oncRPCXdrPointer(oncRPCXdr *xdrs, void **pp, size_t size, oncRPCXdrProc proc);

#ifdef __cplusplus
}
#endif

#endif /* ONCRPC_XDR_H */
