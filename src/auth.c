/*
 * auth.c - authentication bodies, call header encoding, record marking
 * and other helpers shared by client and server.
 */
#include "internal.h"

#include <string.h>

int oncRPCXdrAuthSys(oncRPCXdr *xdrs, oncRPCAuthSysParams *p)
{
    uint32_t n = 0, i;

    if (xdrs->op == ONCRPC_XDR_FREE)
        return 1;
    if (!oncRPCXdrUint32(xdrs, &p->stamp))
        return 0;
    if (xdrs->op == ONCRPC_XDR_ENCODE) {
        size_t l = strlen(p->machinename);
        if (l > ONCRPC_MAX_MACHINENAME)
            return 0;
        n = (uint32_t)l;
    }
    if (!oncRPCXdrUint32(xdrs, &n))
        return 0;
    if (n > ONCRPC_MAX_MACHINENAME) {
        xdrs->error = 1;
        return 0;
    }
    if (!oncRPCXdrOpaque(xdrs, (uint8_t *)p->machinename, n))
        return 0;
    if (xdrs->op == ONCRPC_XDR_DECODE)
        p->machinename[n] = '\0';
    if (!oncRPCXdrUint32(xdrs, &p->uid) || !oncRPCXdrUint32(xdrs, &p->gid) ||
        !oncRPCXdrUint32(xdrs, &p->ngids))
        return 0;
    if (p->ngids > ONCRPC_MAX_GIDS) {
        xdrs->error = 1;
        return 0;
    }
    for (i = 0; i < p->ngids; i++)
        if (!oncRPCXdrUint32(xdrs, &p->gids[i]))
            return 0;
    return 1;
}

int oncRPCIntXdrOpaqueAuth(oncRPCXdr *xdrs, oncRPCOpaqueAuth *a)
{
    if (!oncRPCXdrUint32(xdrs, &a->flavor) || !oncRPCXdrUint32(xdrs, &a->len))
        return 0;
    if (a->len > ONCRPC_MAX_AUTH_BYTES) {
        xdrs->error = 1;
        return 0;
    }
    return oncRPCXdrOpaque(xdrs, a->body, a->len);
}

int oncRPCIntEncodeCall(oncRPCXdr *xdrs, uint32_t xid, uint32_t prog, uint32_t vers,
                        uint32_t proc, const oncRPCOpaqueAuth *cred)
{
    uint32_t mtype = ONCRPC_MSG_CALL, rpcvers = ONCRPC_PROTOCOL_VERSION;
    oncRPCOpaqueAuth c, verf;

    memcpy(&c, cred, sizeof(c));
    memset(&verf, 0, sizeof(verf));
    verf.flavor = ONCRPC_AUTH_NONE;
    return oncRPCXdrUint32(xdrs, &xid) && oncRPCXdrUint32(xdrs, &mtype) &&
           oncRPCXdrUint32(xdrs, &rpcvers) && oncRPCXdrUint32(xdrs, &prog) &&
           oncRPCXdrUint32(xdrs, &vers) && oncRPCXdrUint32(xdrs, &proc) &&
           oncRPCIntXdrOpaqueAuth(xdrs, &c) && oncRPCIntXdrOpaqueAuth(xdrs, &verf);
}

void oncRPCIntPutBe32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

uint32_t oncRPCIntGetBe32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int oncRPCIntGrow(const oncRPCAllocator *a, uint8_t **buf, size_t *cap, size_t need)
{
    size_t nc;
    uint8_t *p;
    if (need == 0)
        need = 1;
    if (*buf != NULL && *cap >= need)
        return 1;
    nc = *cap ? *cap : 4096;
    while (nc < need) {
        if (nc > ((size_t)-1) / 2) {
            nc = need;
            break;
        }
        nc *= 2;
    }
    p = (uint8_t *)oncRPCRealloc(a, *buf, nc);
    if (p == NULL)
        return 0;
    *buf = p;
    *cap = nc;
    return 1;
}

int oncRPCIntRecordExtract(const oncRPCAllocator *a, uint8_t *in, size_t *inlen, size_t max,
                           uint8_t **msg, size_t *msgcap, size_t *msglen)
{
    size_t off = 0, total = 0, w;
    uint32_t h, flen;

    for (;;) {
        if (*inlen - off < 4)
            return 0;
        h = oncRPCIntGetBe32(in + off);
        flen = h & ~ONCRPC_LAST_FRAGMENT;
        if (flen > max - total)
            return -1;
        if (*inlen - off - 4 < flen)
            return 0;
        total += flen;
        off += 4 + (size_t)flen;
        if (h & ONCRPC_LAST_FRAGMENT)
            break;
    }
    if (!oncRPCIntGrow(a, msg, msgcap, total))
        return -1;
    for (w = 0, off = 0;;) {
        h = oncRPCIntGetBe32(in + off);
        flen = h & ~ONCRPC_LAST_FRAGMENT;
        memcpy(*msg + w, in + off + 4, flen);
        w += flen;
        off += 4 + (size_t)flen;
        if (h & ONCRPC_LAST_FRAGMENT)
            break;
    }
    memmove(in, in + off, *inlen - off);
    *inlen -= off;
    *msglen = total;
    return 1;
}

oncRPCStat oncRPCIntUvStat(int uverr, oncRPCStat fallback)
{
    switch (uverr) {
    case 0:
        return ONCRPC_OK;
    case UV_ENOMEM:
    case UV_ENOBUFS:
        return ONCRPC_ERR_NOMEM;
    case UV_EAI_NONAME:
    case UV_EAI_NODATA:
    case UV_EAI_FAIL:
    case UV_EAI_AGAIN:
    case UV_EAI_FAMILY:
        return ONCRPC_ERR_UNKNOWNHOST;
    case UV_ECANCELED:
        return ONCRPC_ERR_CANCELLED;
    case UV_ETIMEDOUT:
        return ONCRPC_ERR_TIMEDOUT;
    case UV_EINVAL:
        return ONCRPC_ERR_INVALID;
    default:
        return fallback;
    }
}
