/*
 * internal.h - declarations shared between liboncrpc source files.
 * Not installed; not part of the public API.
 */
#ifndef ONCRPC_INTERNAL_H
#define ONCRPC_INTERNAL_H

/* This header must be included before any other.
 *
 * glibc hides the POSIX 2008 declarations that libuv's headers need when
 * compiling with strict -std=c99, so ask for them on Linux. Not elsewhere:
 * on FreeBSD and macOS, _POSIX_C_SOURCE would hide BSD interfaces that the
 * system's own headers rely on, and their default environment already has
 * everything. On macOS, _DARWIN_C_SOURCE keeps the full set visible. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#  define _DARWIN_C_SOURCE 1
#endif

#include "oncrpc.h"

/* RPC message constants (RFC 5531, section 9). */
#define ONCRPC_MSG_CALL          0u
#define ONCRPC_MSG_REPLY         1u
#define ONCRPC_MSG_ACCEPTED      0u
#define ONCRPC_MSG_DENIED        1u
#define ONCRPC_REJ_RPC_MISMATCH  0u
#define ONCRPC_REJ_AUTH_ERROR    1u
#define ONCRPC_LAST_FRAGMENT     0x80000000u
#define ONCRPC_READ_CHUNK        65536u

/* opaque_auth with inline storage. */
typedef struct oncRPCOpaqueAuth {
    uint32_t flavor;
    uint32_t len;
    uint8_t  body[ONCRPC_MAX_AUTH_BYTES];
} oncRPCOpaqueAuth;

int  oncRPCIntXdrOpaqueAuth(oncRPCXdr *xdrs, oncRPCOpaqueAuth *a);
int  oncRPCIntEncodeCall(oncRPCXdr *xdrs, uint32_t xid, uint32_t prog, uint32_t vers,
                         uint32_t proc, const oncRPCOpaqueAuth *cred);

void     oncRPCIntPutBe32(uint8_t *p, uint32_t v);
uint32_t oncRPCIntGetBe32(const uint8_t *p);
/* Grow *buf (allocated from a) to hold at least need bytes. */
int      oncRPCIntGrow(const oncRPCAllocator *a, uint8_t **buf, size_t *cap, size_t need);

/*
 * Record marking (RFC 5531 section 11). Looks for one complete record at
 * the start of in[0..*inlen). On success the record's payload is
 * reassembled into *msg (grown as needed), the consumed bytes are removed
 * from in, and 1 is returned. Returns 0 when more input is needed and -1
 * when the record would exceed max bytes or memory runs out.
 */
int oncRPCIntRecordExtract(const oncRPCAllocator *a, uint8_t *in, size_t *inlen, size_t max,
                           uint8_t **msg, size_t *msgcap, size_t *msglen);

/* Map a libuv error to a library status. */
oncRPCStat oncRPCIntUvStat(int uverr, oncRPCStat fallback);

#endif /* ONCRPC_INTERNAL_H */
