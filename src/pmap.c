/*
 * pmap.c - asynchronous client for rpcbind (RFC 1833 versions 3 and 4) and
 * the older port mapper (version 2), which share program 100000 on port 111.
 *
 * One operation asks one question - SET, UNSET or GETADDR (GETPORT in
 * version 2) - starting at the highest protocol version allowed and moving
 * down one version whenever the daemon answers that it does not speak the
 * one asked for (an old portmap daemon knows only version 2).
 */
#include "internal.h"

#include <stdio.h>
#include <string.h>

#define ONCRPC_PMAP_TIMEOUT_MS 5000u
#define ONCRPC_PMAP_RETRY_MS   1000u
#define ONCRPC_NETID_MAX       16u

/*
 * Where the local daemon is. Its Unix socket comes first: there rpcbind
 * learns who is asking, so it records the real owner of a registration
 * (otherwise "unknown") and lets that owner, or root, remove it. The
 * socket's place differs between systems; UDP on the loopback address is
 * the last resort (and the only way on Windows).
 */
static const char *const local_daemons[] = {
#ifndef _WIN32
    "/run/rpcbind.sock",       /* Linux */
    "/var/run/rpcbind.sock",   /* FreeBSD, older Linux */
    "/var/run/rpcb.sock",      /* macOS */
#endif
    NULL
};
#define LOCAL_DAEMONS (sizeof(local_daemons) / sizeof(local_daemons[0]) - 1)

struct rpcbOp {
    uv_loop_t             *loop;
    const oncRPCAllocator *alloc;
    oncRPCClient          *clnt;
    char                  *host;        /* NULL: the local daemon */
    size_t                 attempt;     /* local: which of local_daemons, then UDP */
    int                    localOnly;   /* GETADDR for netid "local" */
    uint32_t               highVers;
    uint32_t               proc;        /* SET, UNSET or GETADDR (= GETPORT) */
    uint32_t               rpcbVers;    /* version being tried */
    uint32_t               lowVers;     /* lowest version to fall back to */
    uint32_t               prog;
    uint32_t               vers;
    oncRPCProto            proto;       /* for the netid when none was given */
    char                   netid[ONCRPC_NETID_MAX];
    char                   uaddr[ONCRPC_UADDR_MAX];
    /* encoding of the arguments */
    uint32_t               mapping[4];  /* version 2: prog, vers, prot, port */
    char                  *argNetid;
    char                  *argAddr;
    char                  *argOwner;
    /* results */
    uint32_t               value;       /* bool, or version 2 port */
    char                  *addrRes;     /* GETADDR's universal address */
    oncRPCIntRpcbCb        cb;
    void                  *data;
    int                    cancelled;
    oncRPCCloseCb          doneCb;      /* after a cancel */
    void                  *doneData;
};

/* ---- Universal addresses ------------------------------------------------------- */

int oncRPCUaddrFromSockaddr(const struct sockaddr *addr, char *buf, size_t len)
{
    char host[64];
    const uint8_t *p;
    int n;

    if (addr == NULL || buf == NULL || len == 0)
        return -1;
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)(const void *)addr;
        if (uv_ip4_name(a, host, sizeof(host)) != 0)
            return -1;
        p = (const uint8_t *)&a->sin_port;
    } else if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)(const void *)addr;
        if (uv_ip6_name(a, host, sizeof(host)) != 0)
            return -1;
        p = (const uint8_t *)&a->sin6_port;
    } else {
        return -1;
    }
    /* The port goes in as its two bytes, high first: "127.0.0.1.4.1" is
     * port 4 * 256 + 1. */
    n = snprintf(buf, len, "%s.%u.%u", host, (unsigned)p[0], (unsigned)p[1]);
    return (n < 0 || (size_t)n >= len) ? -1 : 0;
}

/* Reads a decimal number 0-255 from s[0..n). */
static int port_byte(const char *s, size_t n, unsigned *out)
{
    unsigned v = 0;
    size_t i;
    if (n == 0 || n > 3)
        return -1;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10u + (unsigned)(s[i] - '0');
    }
    if (v > 255u)
        return -1;
    *out = v;
    return 0;
}

int oncRPCUaddrToSockaddr(const char *uaddr, struct sockaddr_storage *addr)
{
    const char *last, *prev = NULL, *p;
    char host[64];
    unsigned hi, lo;
    int port;

    if (uaddr == NULL || addr == NULL)
        return -1;
    last = strrchr(uaddr, '.');
    if (last == NULL)
        return -1;
    for (p = uaddr; p < last; p++)
        if (*p == '.')
            prev = p;
    if (prev == NULL || (size_t)(prev - uaddr) >= sizeof(host) ||
        port_byte(prev + 1, (size_t)(last - prev - 1), &hi) != 0 ||
        port_byte(last + 1, strlen(last + 1), &lo) != 0)
        return -1;
    memcpy(host, uaddr, (size_t)(prev - uaddr));
    host[prev - uaddr] = '\0';
    port = (int)(hi * 256u + lo);
    memset(addr, 0, sizeof(*addr));
    if (strchr(host, ':') == NULL) {
        if (uv_ip4_addr(host, port, (struct sockaddr_in *)addr) != 0)
            return -1;
    } else if (uv_ip6_addr(host, port, (struct sockaddr_in6 *)addr) != 0) {
        return -1;
    }
    return 0;
}

const char *oncRPCNetid(oncRPCProto proto, int family)
{
    if (proto == ONCRPC_PROTO_LOCAL)
        return ONCRPC_NETID_LOCAL;
    if (proto == ONCRPC_PROTO_TCP)
        return family == AF_INET6 ? ONCRPC_NETID_TCP6 : ONCRPC_NETID_TCP;
    if (proto == ONCRPC_PROTO_UDP)
        return family == AF_INET6 ? ONCRPC_NETID_UDP6 : ONCRPC_NETID_UDP;
    return NULL;
}

/* The port from a universal address; -1 if it has none. */
static int uaddr_port(const char *uaddr)
{
    struct sockaddr_storage a;
    const uint8_t *p;
    if (oncRPCUaddrToSockaddr(uaddr, &a) != 0)
        return -1;
    p = a.ss_family == AF_INET6 ? (const uint8_t *)&((struct sockaddr_in6 *)&a)->sin6_port
                                : (const uint8_t *)&((struct sockaddr_in *)&a)->sin_port;
    return (p[0] << 8) | p[1];
}

/* ---- Wire encoding ----------------------------------------------------------- */

static int xdr_mapping(oncRPCXdr *xdrs, void *obj)
{
    uint32_t *m = (uint32_t *)obj;
    return oncRPCXdrUint32(xdrs, &m[0]) && oncRPCXdrUint32(xdrs, &m[1]) &&
           oncRPCXdrUint32(xdrs, &m[2]) && oncRPCXdrUint32(xdrs, &m[3]);
}

/* struct rpcb { prog; vers; string netid; string addr; string owner; } */
static int xdr_rpcb(oncRPCXdr *xdrs, void *obj)
{
    rpcbOp *op = (rpcbOp *)obj;
    return oncRPCXdrUint32(xdrs, &op->prog) && oncRPCXdrUint32(xdrs, &op->vers) &&
           oncRPCXdrString(xdrs, &op->argNetid, ONCRPC_NETID_MAX) &&
           oncRPCXdrString(xdrs, &op->argAddr, ONCRPC_UADDR_MAX) &&
           oncRPCXdrString(xdrs, &op->argOwner, 256);
}

static int xdr_u32(oncRPCXdr *xdrs, void *obj)
{
    /* GETPORT returns an unsigned int, SET/UNSET a bool: same encoding, the
     * bool range check is done on the result. */
    return oncRPCXdrUint32(xdrs, (uint32_t *)obj);
}

static int xdr_addr_result(oncRPCXdr *xdrs, void *obj)
{
    return oncRPCXdrString(xdrs, (char **)obj, ONCRPC_UADDR_MAX);
}

/* ---- One operation ----------------------------------------------------------- */

static void on_closed(void *data)
{
    rpcbOp *op = (rpcbOp *)data;
    oncRPCCloseCb done = op->doneCb;
    void *doneData = op->doneData;
    oncRPCFree(op->alloc, op->addrRes);
    oncRPCFree(op->alloc, op->host);
    oncRPCFree(op->alloc, op);
    if (done)
        done(doneData);
}

static void finish(rpcbOp *op, oncRPCStat st, uint32_t value, const char *uaddr)
{
    if (op->cb)
        op->cb(st, st == ONCRPC_OK ? op->rpcbVers : 0, st == ONCRPC_OK ? value : 0,
               st == ONCRPC_OK ? uaddr : NULL, op->data);
    oncRPCClientClose(op->clnt, on_closed, op);
}

static void on_reply(oncRPCClient *clnt, oncRPCStat st, void *res, void *data);

/* Ask the question in the version being tried. */
static void ask(rpcbOp *op)
{
    oncRPCStat st;

    oncRPCFree(op->alloc, op->addrRes);
    op->addrRes = NULL;
    op->value = 0;
    oncRPCIntClientSetVersion(op->clnt, op->rpcbVers);
    if (op->rpcbVers == ONCRPC_PMAP_VERS) {
        int port = op->uaddr[0] != '\0' ? uaddr_port(op->uaddr) : 0;
        int v6 = strcmp(op->netid, ONCRPC_NETID_TCP6) == 0 ||
                 strcmp(op->netid, ONCRPC_NETID_UDP6) == 0;
        /* Version 2 knows only IPv4 TCP and UDP. */
        if (op->proc == ONCRPC_PMAPPROC_SET && (v6 || port < 0)) {
            finish(op, ONCRPC_ERR_PMAPFAILURE, 0, NULL);
            return;
        }
        op->mapping[0] = op->prog;
        op->mapping[1] = op->vers;
        op->mapping[2] = op->proc == ONCRPC_PMAPPROC_UNSET ? 0u
                         : (strncmp(op->netid, "udp", 3) == 0 ? (uint32_t)ONCRPC_PROTO_UDP
                                                              : (uint32_t)ONCRPC_PROTO_TCP);
        op->mapping[3] = op->proc == ONCRPC_PMAPPROC_SET ? (uint32_t)port : 0u;
        st = oncRPCClientCall(op->clnt, op->proc, xdr_mapping, op->mapping, xdr_u32, &op->value,
                              on_reply, op);
    } else {
        op->argNetid = op->netid;
        op->argAddr = op->uaddr;
        op->argOwner = (char *)"";
        if (op->proc == ONCRPC_RPCBPROC_GETADDR)
            st = oncRPCClientCall(op->clnt, op->proc, xdr_rpcb, op, xdr_addr_result,
                                  &op->addrRes, on_reply, op);
        else
            st = oncRPCClientCall(op->clnt, op->proc, xdr_rpcb, op, xdr_u32, &op->value,
                                  on_reply, op);
    }
    if (st != ONCRPC_OK)
        finish(op, ONCRPC_ERR_PMAPFAILURE, 0, NULL);
}

static void on_reply(oncRPCClient *clnt, oncRPCStat st, void *res, void *data)
{
    rpcbOp *op = (rpcbOp *)data;
    (void)clnt;
    (void)res;

    if (op->cancelled || st == ONCRPC_ERR_CANCELLED)
        return;
    if ((st == ONCRPC_ERR_PROGVERSMISMATCH || st == ONCRPC_ERR_PROCUNAVAIL) &&
        op->rpcbVers > op->lowVers) {
        op->rpcbVers--;
        ask(op);
        return;
    }
    if (st != ONCRPC_OK) {
        finish(op, st == ONCRPC_ERR_UNKNOWNHOST ? st : ONCRPC_ERR_PMAPFAILURE, 0, NULL);
        return;
    }
    if (op->proc != ONCRPC_RPCBPROC_GETADDR) {
        if (op->value > 1)
            finish(op, ONCRPC_ERR_PMAPFAILURE, 0, NULL);
        else
            finish(op, ONCRPC_OK, op->value, NULL);
        return;
    }
    if (op->rpcbVers == ONCRPC_PMAP_VERS) {
        if (op->value == 0)
            finish(op, ONCRPC_ERR_PROGNOTREGISTERED, 0, NULL);
        else if (op->value > 65535u)
            finish(op, ONCRPC_ERR_PMAPFAILURE, 0, NULL);
        else
            finish(op, ONCRPC_OK, op->value, NULL);
        return;
    }
    if (op->addrRes == NULL || op->addrRes[0] == '\0') {
        finish(op, ONCRPC_ERR_PROGNOTREGISTERED, 0, NULL);
    } else if (strcmp(op->netid, ONCRPC_NETID_LOCAL) == 0) {
        finish(op, ONCRPC_OK, 0, op->addrRes);   /* a path: no port */
    } else {
        int port = uaddr_port(op->addrRes);
        if (port <= 0)
            finish(op, ONCRPC_ERR_PMAPFAILURE, 0, NULL);
        else
            finish(op, ONCRPC_OK, (uint32_t)port, op->addrRes);
    }
}

static oncRPCStat open_attempt(rpcbOp *op);

/* The daemon's address is known: pick the netid and ask. */
static void on_ready(oncRPCClient *clnt, oncRPCStat st, void *data)
{
    rpcbOp *op = (rpcbOp *)data;

    if (op->cancelled)
        return;
    if (st != ONCRPC_OK && op->host == NULL && op->attempt + 1 < LOCAL_DAEMONS + !op->localOnly) {
        /* Not at this socket: try the next place. */
        oncRPCClientClose(clnt, NULL, NULL);
        op->attempt++;
        st = open_attempt(op);
        if (st != ONCRPC_OK)
            finish(op, st, 0, NULL);
        return;
    }
    if (st != ONCRPC_OK) {
        finish(op, st == ONCRPC_ERR_UNKNOWNHOST ? st : ONCRPC_ERR_PMAPFAILURE, 0, NULL);
        return;
    }
    if (op->netid[0] == '\0' && op->proc != ONCRPC_RPCBPROC_UNSET)
        snprintf(op->netid, sizeof(op->netid), "%s",
                 oncRPCNetid(op->proto, oncRPCIntClientFamily(clnt)));
    ask(op);
}

/* Connect to the daemon for this attempt. The protocol version starts
 * again at the highest for each place tried. */
static oncRPCStat open_attempt(rpcbOp *op)
{
    oncRPCProto transport = ONCRPC_PROTO_UDP;
    const char *where;
    oncRPCStat st;

    if (op->host == NULL && local_daemons[op->attempt] != NULL) {
        where = local_daemons[op->attempt];
        transport = ONCRPC_PROTO_LOCAL;
    } else {
        where = op->host != NULL ? op->host : "127.0.0.1";
        /* rpcbind answers GETADDR for the transport the question arrives
         * on (the netid in the request does not choose it), so ask about a
         * TCP service over TCP. SET and UNSET go by the netid. */
        if (op->proc == ONCRPC_RPCBPROC_GETADDR)
            transport = op->netid[0] != '\0'
                            ? (strncmp(op->netid, "tcp", 3) == 0 ? ONCRPC_PROTO_TCP
                                                                 : ONCRPC_PROTO_UDP)
                            : op->proto;
    }
    op->rpcbVers = op->highVers;
    st = oncRPCClientCreate(op->loop, op->alloc, where, ONCRPC_PMAP_PORT, ONCRPC_PMAP_PROG,
                            op->highVers, transport, on_ready, op, &op->clnt);
    if (st != ONCRPC_OK)
        return st;
    oncRPCClientSetTimeout(op->clnt, ONCRPC_PMAP_TIMEOUT_MS);
    oncRPCClientSetRetry(op->clnt, ONCRPC_PMAP_RETRY_MS);
    return ONCRPC_OK;
}

oncRPCStat oncRPCIntRpcbStart(uv_loop_t *loop, const oncRPCAllocator *alloc, const char *host,
                              uint32_t proc, uint32_t highVers, uint32_t lowVers, uint32_t prog,
                              uint32_t vers, const char *netid, oncRPCProto proto,
                              const char *uaddr, oncRPCIntRpcbCb cb, void *data, rpcbOp **out)
{
    rpcbOp *op;
    oncRPCStat st;

    if (out != NULL)
        *out = NULL;
    if (loop == NULL || alloc == NULL || lowVers < ONCRPC_PMAP_VERS ||
        highVers > ONCRPC_RPCB_VERS4 || lowVers > highVers ||
        (netid != NULL && strlen(netid) >= ONCRPC_NETID_MAX) ||
        (uaddr != NULL && strlen(uaddr) >= ONCRPC_UADDR_MAX) ||
        (proc == ONCRPC_RPCBPROC_SET && (netid == NULL || uaddr == NULL)) ||
        (netid == NULL && proc == ONCRPC_RPCBPROC_GETADDR && oncRPCNetid(proto, AF_INET) == NULL))
        return ONCRPC_ERR_INVALID;
    op = (rpcbOp *)oncRPCAlloc(alloc, sizeof(*op));
    if (op == NULL)
        return ONCRPC_ERR_NOMEM;
    op->loop = loop;
    op->alloc = alloc;
    op->proc = proc;
    op->highVers = highVers;
    op->lowVers = lowVers;
    op->prog = prog;
    op->vers = vers;
    op->proto = proto;
    if (netid != NULL)
        snprintf(op->netid, sizeof(op->netid), "%s", netid);
    if (uaddr != NULL)
        snprintf(op->uaddr, sizeof(op->uaddr), "%s", uaddr);
    op->cb = cb;
    op->data = data;
    /* A local service can only be looked up through the local socket. */
    op->localOnly = proc == ONCRPC_RPCBPROC_GETADDR && netid != NULL &&
                    strcmp(netid, ONCRPC_NETID_LOCAL) == 0;
    if (op->localOnly) {
        host = NULL;
        if (LOCAL_DAEMONS == 0) {
            oncRPCFree(alloc, op);
            return ONCRPC_ERR_INVALID;
        }
    }
    if (host != NULL && (op->host = oncRPCStrdup(alloc, host)) == NULL) {
        oncRPCFree(alloc, op);
        return ONCRPC_ERR_NOMEM;
    }
    st = open_attempt(op);
    if (st != ONCRPC_OK) {
        oncRPCFree(alloc, op->host);
        oncRPCFree(alloc, op);
        return st;
    }
    if (out != NULL)
        *out = op;
    return ONCRPC_OK;
}

void oncRPCIntRpcbCancel(rpcbOp *op, oncRPCCloseCb done, void *data)
{
    if (op == NULL || op->cancelled)
        return;
    op->cancelled = 1;
    op->doneCb = done;
    op->doneData = data;
    oncRPCClientClose(op->clnt, on_closed, op);
}

/* ---- Public functions --------------------------------------------------------- */

/* Adapts the internal result to oncRPCPmapCb. */
typedef struct pmapCall {
    const oncRPCAllocator *alloc;
    oncRPCPmapCb           cb;
    oncRPCRpcbAddrCb       addrCb;
    void                  *data;
} pmapCall;

static void on_pmap_done(oncRPCStat st, uint32_t rpcbVers, uint32_t value, const char *uaddr,
                         void *data)
{
    pmapCall *pc = (pmapCall *)data;
    (void)rpcbVers;
    if (pc->addrCb)
        pc->addrCb(st, uaddr, pc->data);
    else if (pc->cb)
        pc->cb(st, value, pc->data);
    oncRPCFree(pc->alloc, pc);
}

static oncRPCStat start_public(uv_loop_t *loop, const oncRPCAllocator *alloc, const char *host,
                               uint32_t proc, uint32_t high, uint32_t low, uint32_t prog,
                               uint32_t vers, const char *netid, oncRPCProto proto,
                               const char *uaddr, oncRPCPmapCb cb, oncRPCRpcbAddrCb addrCb,
                               void *data)
{
    pmapCall *pc;
    oncRPCStat st;

    if (alloc == NULL)
        return ONCRPC_ERR_INVALID;
    pc = (pmapCall *)oncRPCAlloc(alloc, sizeof(*pc));
    if (pc == NULL)
        return ONCRPC_ERR_NOMEM;
    pc->alloc = alloc;
    pc->cb = cb;
    pc->addrCb = addrCb;
    pc->data = data;
    st = oncRPCIntRpcbStart(loop, alloc, host, proc, high, low, prog, vers, netid, proto, uaddr,
                            on_pmap_done, pc, NULL);
    if (st != ONCRPC_OK)
        oncRPCFree(alloc, pc);
    return st;
}

oncRPCStat oncRPCPmapGetPort(uv_loop_t *loop, const oncRPCAllocator *alloc, const char *host,
                             uint32_t prog, uint32_t vers, oncRPCProto proto, oncRPCPmapCb cb,
                             void *data)
{
    const char *netid = oncRPCNetid(proto, AF_INET);
    if (netid == NULL)
        return ONCRPC_ERR_INVALID;
    return start_public(loop, alloc, host, ONCRPC_PMAPPROC_GETPORT, ONCRPC_PMAP_VERS,
                        ONCRPC_PMAP_VERS, prog, vers, netid, proto, NULL, cb, NULL, data);
}

oncRPCStat oncRPCPmapSet(uv_loop_t *loop, const oncRPCAllocator *alloc, uint32_t prog,
                         uint32_t vers, oncRPCProto proto, uint16_t port, oncRPCPmapCb cb,
                         void *data)
{
    char uaddr[ONCRPC_UADDR_MAX];
    const char *netid = oncRPCNetid(proto, AF_INET);
    if (netid == NULL)
        return ONCRPC_ERR_INVALID;
    snprintf(uaddr, sizeof(uaddr), "0.0.0.0.%u.%u", (unsigned)(port >> 8),
             (unsigned)(port & 0xffu));
    return start_public(loop, alloc, NULL, ONCRPC_PMAPPROC_SET, ONCRPC_PMAP_VERS,
                        ONCRPC_PMAP_VERS, prog, vers, netid, proto, uaddr, cb, NULL, data);
}

oncRPCStat oncRPCPmapUnset(uv_loop_t *loop, const oncRPCAllocator *alloc, uint32_t prog,
                           uint32_t vers, oncRPCPmapCb cb, void *data)
{
    return start_public(loop, alloc, NULL, ONCRPC_PMAPPROC_UNSET, ONCRPC_PMAP_VERS,
                        ONCRPC_PMAP_VERS, prog, vers, "", ONCRPC_PROTO_TCP, "", cb, NULL, data);
}

oncRPCStat oncRPCRpcbGetAddr(uv_loop_t *loop, const oncRPCAllocator *alloc, const char *host,
                             uint32_t prog, uint32_t vers, const char *netid,
                             oncRPCRpcbAddrCb cb, void *data)
{
    if (netid == NULL)
        return ONCRPC_ERR_INVALID;
    return start_public(loop, alloc, host, ONCRPC_RPCBPROC_GETADDR, ONCRPC_RPCB_VERS4,
                        ONCRPC_RPCB_VERS, prog, vers, netid, ONCRPC_PROTO_TCP, NULL, NULL, cb,
                        data);
}

oncRPCStat oncRPCRpcbSet(uv_loop_t *loop, const oncRPCAllocator *alloc, uint32_t prog,
                         uint32_t vers, const char *netid, const char *uaddr, oncRPCPmapCb cb,
                         void *data)
{
    if (netid == NULL || uaddr == NULL)
        return ONCRPC_ERR_INVALID;
    return start_public(loop, alloc, NULL, ONCRPC_RPCBPROC_SET, ONCRPC_RPCB_VERS4,
                        ONCRPC_RPCB_VERS, prog, vers, netid, ONCRPC_PROTO_TCP, uaddr, cb, NULL,
                        data);
}

oncRPCStat oncRPCRpcbUnset(uv_loop_t *loop, const oncRPCAllocator *alloc, uint32_t prog,
                           uint32_t vers, const char *netid, oncRPCPmapCb cb, void *data)
{
    return start_public(loop, alloc, NULL, ONCRPC_RPCBPROC_UNSET, ONCRPC_RPCB_VERS4,
                        ONCRPC_RPCB_VERS, prog, vers, netid != NULL ? netid : "",
                        ONCRPC_PROTO_TCP, "", cb, NULL, data);
}
