/*
 * pmap.c - asynchronous port mapper (RFC 1833 version 2) client.
 */
#include "internal.h"

#define ONCRPC_PMAP_TIMEOUT_MS 5000u
#define ONCRPC_PMAP_RETRY_MS   1000u

typedef struct pmapOp {
    const oncRPCAllocator *alloc;
    oncRPCClient          *clnt;
    uint32_t               proc;
    uint32_t               mapping[4];   /* prog, vers, prot, port */
    uint32_t               result;
    oncRPCPmapCb           cb;
    void                  *data;
} pmapOp;

static int xdr_mapping(oncRPCXdr *xdrs, void *obj)
{
    uint32_t *m = (uint32_t *)obj;
    return oncRPCXdrUint32(xdrs, &m[0]) && oncRPCXdrUint32(xdrs, &m[1]) &&
           oncRPCXdrUint32(xdrs, &m[2]) && oncRPCXdrUint32(xdrs, &m[3]);
}

static int xdr_result(oncRPCXdr *xdrs, void *obj)
{
    /* GETPORT returns an unsigned int, SET/UNSET a bool: same encoding,
     * the bool range check is done below. */
    return oncRPCXdrUint32(xdrs, (uint32_t *)obj);
}

static void on_closed(void *data)
{
    pmapOp *op = (pmapOp *)data;
    oncRPCFree(op->alloc, op);
}

static void on_reply(oncRPCClient *clnt, oncRPCStat st, void *res, void *data)
{
    pmapOp *op = (pmapOp *)data;
    uint32_t value = op->result;
    (void)res;

    if (st == ONCRPC_ERR_CANCELLED)
        return;   /* we are closing it ourselves */
    if (st == ONCRPC_OK) {
        if (op->proc == ONCRPC_PMAPPROC_GETPORT) {
            if (value == 0)
                st = ONCRPC_ERR_PROGNOTREGISTERED;
            else if (value > 65535u)
                st = ONCRPC_ERR_PMAPFAILURE;
        } else if (value > 1) {
            st = ONCRPC_ERR_PMAPFAILURE;
        }
    } else if (st != ONCRPC_ERR_UNKNOWNHOST) {
        st = ONCRPC_ERR_PMAPFAILURE;
    }
    if (op->cb)
        op->cb(st, st == ONCRPC_OK ? value : 0, op->data);
    oncRPCClientClose(clnt, on_closed, op);
}

static oncRPCStat start(uv_loop_t *loop, const oncRPCAllocator *alloc, const char *host,
                        uint32_t proc, uint32_t prog, uint32_t vers, uint32_t prot,
                        uint32_t port, oncRPCPmapCb cb, void *data)
{
    pmapOp *op;
    oncRPCStat st;

    if (loop == NULL || alloc == NULL || host == NULL)
        return ONCRPC_ERR_INVALID;
    op = (pmapOp *)oncRPCAlloc(alloc, sizeof(*op));
    if (op == NULL)
        return ONCRPC_ERR_NOMEM;
    op->alloc = alloc;
    op->proc = proc;
    op->mapping[0] = prog;
    op->mapping[1] = vers;
    op->mapping[2] = prot;
    op->mapping[3] = port;
    op->cb = cb;
    op->data = data;
    st = oncRPCClientCreate(loop, alloc, host, ONCRPC_PMAP_PORT, ONCRPC_PMAP_PROG,
                            ONCRPC_PMAP_VERS, ONCRPC_PROTO_UDP, NULL, NULL, &op->clnt);
    if (st != ONCRPC_OK) {
        oncRPCFree(alloc, op);
        return st;
    }
    oncRPCClientSetTimeout(op->clnt, ONCRPC_PMAP_TIMEOUT_MS);
    oncRPCClientSetRetry(op->clnt, ONCRPC_PMAP_RETRY_MS);
    st = oncRPCClientCall(op->clnt, proc, xdr_mapping, op->mapping, xdr_result, &op->result,
                          on_reply, op);
    if (st != ONCRPC_OK) {
        oncRPCClientClose(op->clnt, on_closed, op);
        return st;
    }
    return ONCRPC_OK;
}

oncRPCStat oncRPCPmapGetPort(uv_loop_t *loop, const oncRPCAllocator *alloc, const char *host,
                             uint32_t prog, uint32_t vers, oncRPCProto proto, oncRPCPmapCb cb,
                             void *data)
{
    return start(loop, alloc, host, ONCRPC_PMAPPROC_GETPORT, prog, vers, (uint32_t)proto, 0,
                 cb, data);
}

oncRPCStat oncRPCPmapSet(uv_loop_t *loop, const oncRPCAllocator *alloc, uint32_t prog,
                         uint32_t vers, oncRPCProto proto, uint16_t port, oncRPCPmapCb cb,
                         void *data)
{
    return start(loop, alloc, "127.0.0.1", ONCRPC_PMAPPROC_SET, prog, vers, (uint32_t)proto,
                 port, cb, data);
}

oncRPCStat oncRPCPmapUnset(uv_loop_t *loop, const oncRPCAllocator *alloc, uint32_t prog,
                           uint32_t vers, oncRPCPmapCb cb, void *data)
{
    return start(loop, alloc, "127.0.0.1", ONCRPC_PMAPPROC_UNSET, prog, vers, 0, 0, cb, data);
}
