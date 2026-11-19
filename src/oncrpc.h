/*
 * oncrpc.h - asynchronous ONC RPC version 2 (RFC 5531) on libuv.
 *
 * liboncrpc runs on an application-supplied libuv loop alongside whatever
 * else uses that loop. No function blocks: network operations start work
 * and report completion through callbacks, which are always invoked from
 * the loop (never from inside the function that started the operation).
 *
 * All memory is obtained from an application-supplied oncRPCAllocator; the
 * library itself never calls malloc, realloc or free. (libuv's own internal
 * allocations, e.g. inside uv_getaddrinfo, are governed by libuv.)
 *
 * Conventions
 *   - Functions starting an operation return an oncRPCStat. ONCRPC_OK means
 *     the operation started and its callback will be called exactly once;
 *     any other value means it did not start and no callback will follow.
 *   - XDR-style functions return non-zero on success, 0 on failure.
 *   - Handles (client, server) are closed asynchronously with
 *     oncRPCClientClose / oncRPCServerClose; their memory is released just
 *     before the close callback runs.
 *   - Nothing is thread-safe: use a handle only from its loop's thread.
 */
#ifndef ONCRPC_H
#define ONCRPC_H

#include <stddef.h>
#include <stdint.h>

#include <uv.h>

#include "xdr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ONCRPC_VERSION_STRING     "3.0.0"
#define ONCRPC_PROTOCOL_VERSION   2u          /* RPC message protocol version */
#define ONCRPC_MAX_AUTH_BYTES     400u        /* opaque_auth body limit */
#define ONCRPC_MAX_MACHINENAME    255u
#define ONCRPC_MAX_GIDS           16u
#define ONCRPC_DEFAULT_MAX_RECORD (16u * 1024u * 1024u)
#define ONCRPC_DEFAULT_TIMEOUT_MS 25000u
#define ONCRPC_DEFAULT_RETRY_MS   1000u

/* Server limit defaults (see oncRPCServerLimits). */
#define ONCRPC_DEFAULT_MAX_CONNECTIONS      1024u
#define ONCRPC_DEFAULT_MAX_REQUESTS         4096u
#define ONCRPC_DEFAULT_MAX_REQUESTS_PER_CONNECTION 64u
#define ONCRPC_DEFAULT_IDLE_TIMEOUT_MS      300000u
#define ONCRPC_DEFAULT_DRC_ENTRIES          1024u
#define ONCRPC_DEFAULT_DRC_BYTES            (4u * 1024u * 1024u)
#define ONCRPC_DEFAULT_DRC_TTL_MS           120000u
#define ONCRPC_UDP_MAX            65536u

/* Port mapper (RFC 1833, version 2) */
#define ONCRPC_PMAP_PROG          100000u
#define ONCRPC_PMAP_VERS          2u
#define ONCRPC_PMAP_PORT          111u
#define ONCRPC_PMAPPROC_NULL      0u
#define ONCRPC_PMAPPROC_SET       1u
#define ONCRPC_PMAPPROC_UNSET     2u
#define ONCRPC_PMAPPROC_GETPORT   3u

/* Transport protocols (values are the IP protocol numbers). */
typedef enum oncRPCProto {
    ONCRPC_PROTO_TCP = 6,
    ONCRPC_PROTO_UDP = 17
} oncRPCProto;

/* Authentication flavors. The library implements NONE and SYS. */
typedef enum oncRPCAuthFlavor {
    ONCRPC_AUTH_NONE  = 0,
    ONCRPC_AUTH_SYS   = 1,
    ONCRPC_AUTH_SHORT = 2,
    ONCRPC_AUTH_DH    = 3,
    ONCRPC_RPCSEC_GSS = 6
} oncRPCAuthFlavor;

/* accept_stat values of an accepted reply. */
typedef enum oncRPCAcceptStat {
    ONCRPC_ACCEPT_SUCCESS       = 0,
    ONCRPC_ACCEPT_PROG_UNAVAIL  = 1,
    ONCRPC_ACCEPT_PROG_MISMATCH = 2,
    ONCRPC_ACCEPT_PROC_UNAVAIL  = 3,
    ONCRPC_ACCEPT_GARBAGE_ARGS  = 4,
    ONCRPC_ACCEPT_SYSTEM_ERR    = 5
} oncRPCAcceptStat;

/* auth_stat values of an AUTH_ERROR rejection. */
typedef enum oncRPCAuthStat {
    ONCRPC_AUTH_OK           = 0,
    ONCRPC_AUTH_BADCRED      = 1,
    ONCRPC_AUTH_REJECTEDCRED = 2,
    ONCRPC_AUTH_BADVERF      = 3,
    ONCRPC_AUTH_REJECTEDVERF = 4,
    ONCRPC_AUTH_TOOWEAK      = 5,
    ONCRPC_AUTH_INVALIDRESP  = 6,
    ONCRPC_AUTH_FAILED       = 7
} oncRPCAuthStat;

/* Library status codes. */
typedef enum oncRPCStat {
    ONCRPC_OK = 0,
    ONCRPC_ERR_CANTENCODE,        /* arguments could not be encoded */
    ONCRPC_ERR_CANTDECODE,        /* reply/results could not be decoded */
    ONCRPC_ERR_CANTSEND,
    ONCRPC_ERR_CANTRECV,          /* connection lost before the reply */
    ONCRPC_ERR_TIMEDOUT,
    ONCRPC_ERR_VERSMISMATCH,      /* server rejected RPC protocol version */
    ONCRPC_ERR_AUTHERROR,         /* server rejected credentials */
    ONCRPC_ERR_PROGUNAVAIL,
    ONCRPC_ERR_PROGVERSMISMATCH,  /* program version not supported */
    ONCRPC_ERR_PROCUNAVAIL,
    ONCRPC_ERR_CANTDECODEARGS,    /* server could not decode arguments */
    ONCRPC_ERR_SYSTEMERROR,       /* server-side error */
    ONCRPC_ERR_UNKNOWNHOST,
    ONCRPC_ERR_CANTCONNECT,
    ONCRPC_ERR_PMAPFAILURE,       /* port mapper unreachable or failed */
    ONCRPC_ERR_PROGNOTREGISTERED, /* port mapper has no such program */
    ONCRPC_ERR_NOMEM,
    ONCRPC_ERR_INVALID,           /* invalid argument or state */
    ONCRPC_ERR_CANCELLED,         /* handle closed before completion */
    ONCRPC_ERR_FAILED             /* unspecified failure */
} oncRPCStat;

/* Details of a call's outcome (see oncRPCClientLastError). */
typedef struct oncRPCError {
    oncRPCStat     stat;
    uint32_t       low, high;   /* supported range for *_VERSMISMATCH */
    oncRPCAuthStat why;         /* reason for ONCRPC_ERR_AUTHERROR */
    int            uvError;     /* libuv error code behind a transport failure, or 0 */
} oncRPCError;

/* AUTH_SYS credential body. */
typedef struct oncRPCAuthSysParams {
    uint32_t stamp;
    char     machinename[ONCRPC_MAX_MACHINENAME + 1];
    uint32_t uid;
    uint32_t gid;
    uint32_t ngids;
    uint32_t gids[ONCRPC_MAX_GIDS];
} oncRPCAuthSysParams;

typedef void (*oncRPCCloseCb)(void *data);

/* ---- Library ------------------------------------------------------------ */

ONCRPC_API const char *oncRPCStrError(oncRPCStat stat);
ONCRPC_API const char *oncRPCVersion(void);
/* XDR routine for an AUTH_SYS credential body. */
ONCRPC_API int oncRPCXdrAuthSys(oncRPCXdr *xdrs, oncRPCAuthSysParams *p);

/* ---- Client ------------------------------------------------------------- */

typedef struct oncRPCClient oncRPCClient;

/* Connection outcome: ONCRPC_OK once ready, otherwise why it failed. */
typedef void (*oncRPCClientReadyCb)(oncRPCClient *clnt, oncRPCStat status, void *data);
/* Call outcome. res is the pointer given to oncRPCClientCall; on
 * ONCRPC_OK it holds the decoded results. */
typedef void (*oncRPCCallCb)(oncRPCClient *clnt, oncRPCStat status, void *res, void *data);

/*
 * Create a client for program prog, version vers on host (name or numeric
 * address). If port is 0 the port is looked up with the host's port mapper.
 * Resolution, port lookup and connection happen asynchronously; readyCb
 * (may be NULL) reports the outcome. Calls may be issued immediately: they
 * are queued until the client is ready. data is passed to readyCb and is
 * available through oncRPCClientData().
 */
ONCRPC_API oncRPCStat oncRPCClientCreate(uv_loop_t *loop, const oncRPCAllocator *alloc,
                                         const char *host, uint16_t port, uint32_t prog,
                                         uint32_t vers, oncRPCProto proto,
                                         oncRPCClientReadyCb readyCb, void *data,
                                         oncRPCClient **clnt);

/*
 * Close the client. Outstanding calls complete with ONCRPC_ERR_CANCELLED,
 * then cb (may be NULL) runs and the handle is gone.
 */
ONCRPC_API void oncRPCClientClose(oncRPCClient *clnt, oncRPCCloseCb cb, void *data);

/*
 * Start a call of procedure proc. args is encoded with xargs before this
 * function returns, so it need not outlive the call. When the reply
 * arrives, results are decoded into res with xres and cb runs. res must
 * stay valid until then, should be zero-initialised, and on ONCRPC_OK must
 * eventually be released with oncRPCXdrFree(alloc, xres, res). xargs/xres
 * may be NULL for void. Many calls may be outstanding at once.
 */
ONCRPC_API oncRPCStat oncRPCClientCall(oncRPCClient *clnt, uint32_t proc,
                                       oncRPCXdrProc xargs, const void *args,
                                       oncRPCXdrProc xres, void *res,
                                       oncRPCCallCb cb, void *data);

/* Total time allowed per call, from oncRPCClientCall (default 25 s). */
ONCRPC_API void oncRPCClientSetTimeout(oncRPCClient *clnt, uint32_t ms);
/* UDP retransmission interval (default 1 s). */
ONCRPC_API void oncRPCClientSetRetry(oncRPCClient *clnt, uint32_t ms);
/* Largest TCP reply record accepted (default ONCRPC_DEFAULT_MAX_RECORD). */
ONCRPC_API void oncRPCClientSetMaxRecord(oncRPCClient *clnt, size_t bytes);
/* Use AUTH_NONE (the default) for subsequent calls. */
ONCRPC_API oncRPCStat oncRPCClientSetAuthNone(oncRPCClient *clnt);
/* Use AUTH_SYS with the given identity. ngids is at most ONCRPC_MAX_GIDS. */
ONCRPC_API oncRPCStat oncRPCClientSetAuthSys(oncRPCClient *clnt, const char *machinename,
                                             uint32_t uid, uint32_t gid, uint32_t ngids,
                                             const uint32_t *gids);
/* Use AUTH_SYS with the identity of the current process. */
ONCRPC_API oncRPCStat oncRPCClientSetAuthSysDefault(oncRPCClient *clnt);
/* Details of the most recently completed call; valid in call callbacks. */
ONCRPC_API const oncRPCError *oncRPCClientLastError(const oncRPCClient *clnt);
ONCRPC_API void *oncRPCClientData(const oncRPCClient *clnt);
ONCRPC_API const oncRPCAllocator *oncRPCClientAllocator(const oncRPCClient *clnt);
/* Number of calls not yet completed. */
ONCRPC_API size_t oncRPCClientPending(const oncRPCClient *clnt);

/* ---- Port mapper --------------------------------------------------------- */

/* value: the port (GetPort) or the port mapper's boolean answer (Set/Unset). */
typedef void (*oncRPCPmapCb)(oncRPCStat status, uint32_t value, void *data);

ONCRPC_API oncRPCStat oncRPCPmapGetPort(uv_loop_t *loop, const oncRPCAllocator *alloc,
                                        const char *host, uint32_t prog, uint32_t vers,
                                        oncRPCProto proto, oncRPCPmapCb cb, void *data);
/* Register/unregister with the port mapper on the local host. */
ONCRPC_API oncRPCStat oncRPCPmapSet(uv_loop_t *loop, const oncRPCAllocator *alloc,
                                    uint32_t prog, uint32_t vers, oncRPCProto proto,
                                    uint16_t port, oncRPCPmapCb cb, void *data);
ONCRPC_API oncRPCStat oncRPCPmapUnset(uv_loop_t *loop, const oncRPCAllocator *alloc,
                                      uint32_t prog, uint32_t vers,
                                      oncRPCPmapCb cb, void *data);

/* ---- Server ------------------------------------------------------------- */

typedef struct oncRPCServer  oncRPCServer;
typedef struct oncRPCSvcPriv oncRPCSvcPriv;

/*
 * An incoming call. It stays valid until it is answered with
 * oncRPCSvcReply or an oncRPCSvcErr* function, or released with
 * oncRPCSvcDrop - which may happen inside the dispatch routine or at any
 * later time (asynchronous replies). Every request must be finished exactly
 * once; a server does not complete closing while requests are outstanding.
 */
typedef struct oncRPCSvcReq {
    uint32_t                   xid;
    uint32_t                   prog;
    uint32_t                   vers;
    uint32_t                   proc;
    oncRPCProto                proto;
    uint32_t                   credFlavor;  /* oncRPCAuthFlavor */
    const oncRPCAuthSysParams *authSys;     /* non-NULL for AUTH_SYS */
    void                      *ctx;         /* from oncRPCServerRegister */
    oncRPCServer              *server;
    oncRPCSvcPriv             *priv;        /* internal */
} oncRPCSvcReq;

/* Dispatch routine for one program version. */
typedef void (*oncRPCDispatchFn)(oncRPCSvcReq *req);
typedef void (*oncRPCServerPmapCb)(oncRPCServer *srv, oncRPCStat status, void *data);

ONCRPC_API oncRPCStat oncRPCServerCreate(uv_loop_t *loop, const oncRPCAllocator *alloc,
                                         oncRPCServer **srv);
/* Stop listening, close all connections; cb runs once every handle is
 * closed and every outstanding request has been finished. Replies to
 * requests finished after this point are discarded. */
ONCRPC_API void oncRPCServerClose(oncRPCServer *srv, oncRPCCloseCb cb, void *data);
/* Listen on numeric address ip (NULL = "0.0.0.0"; "::" for IPv6) and port
 * (0 = any free port). May be called several times. */
ONCRPC_API oncRPCStat oncRPCServerListen(oncRPCServer *srv, oncRPCProto proto,
                                         const char *ip, uint16_t port);
/* Port of the first listener of the given protocol, 0 if none. */
ONCRPC_API uint16_t oncRPCServerPort(const oncRPCServer *srv, oncRPCProto proto);
/* Serve a program version with dispatch; ctx is passed in each request. */
ONCRPC_API oncRPCStat oncRPCServerRegister(oncRPCServer *srv, uint32_t prog, uint32_t vers,
                                           oncRPCDispatchFn dispatch, void *ctx);
ONCRPC_API oncRPCStat oncRPCServerUnregister(oncRPCServer *srv, uint32_t prog, uint32_t vers);
/* Register every listener for prog/vers with the local port mapper
 * (replacing stale entries). cb (may be NULL) reports the outcome. */
ONCRPC_API oncRPCStat oncRPCServerPmapRegister(oncRPCServer *srv, uint32_t prog,
                                               uint32_t vers, oncRPCServerPmapCb cb,
                                               void *data);
ONCRPC_API oncRPCStat oncRPCServerPmapUnregister(oncRPCServer *srv, uint32_t prog,
                                                 uint32_t vers, oncRPCServerPmapCb cb,
                                                 void *data);
/*
 * Limits that protect a server from busy or hostile clients. For the count
 * and time limits, 0 means "no limit". Defaults are the ONCRPC_DEFAULT_*
 * values above.
 */
typedef struct oncRPCServerLimits {
    /* TCP connections open at once; further connections are closed as
     * soon as they are accepted. */
    size_t   maxConnections;
    /* Requests being handled at once (dispatched, not yet finished), over
     * all transports. When reached, TCP connections stop being read and new
     * UDP requests are dropped (the clients retransmit). */
    size_t   maxRequests;
    /* Requests being handled at once for one TCP connection; when reached,
     * that connection stops being read until one is finished. */
    size_t   maxRequestsPerConnection;
    /* Close TCP connections that have had no traffic and no outstanding
     * request for this long. */
    uint32_t idleTimeoutMs;
    /* Largest TCP request record accepted (must not be 0). */
    size_t   maxRecord;
    /*
     * UDP duplicate-request cache. A UDP client that receives no reply
     * resends its request; without the cache the procedure would run again.
     * With it, a resent request is ignored while the original is still being
     * handled, and answered with the original reply once there is one. Up to
     * duplicateCacheEntries requests and duplicateCacheBytes of replies are
     * remembered, each for duplicateCacheTtlMs after its reply; the oldest
     * replies make way for new ones. duplicateCacheEntries 0 turns the cache
     * off; duplicateCacheBytes 0 or duplicateCacheTtlMs 0 removes that limit.
     */
    size_t   duplicateCacheEntries;
    size_t   duplicateCacheBytes;
    uint32_t duplicateCacheTtlMs;
} oncRPCServerLimits;

/* What a server has done, for monitoring and tests. */
typedef struct oncRPCServerStats {
    uint64_t requests;               /* requests received (not counting duplicates) */
    uint64_t duplicatesReplayed;     /* UDP resends answered from the cache */
    uint64_t duplicatesDropped;      /* UDP resends of requests still being handled */
    uint64_t requestsDroppedBusy;    /* UDP requests dropped at maxRequests */
    uint64_t connectionsAccepted;
    uint64_t connectionsRefused;     /* closed at once because of maxConnections */
    uint64_t connectionsIdleClosed;  /* closed by idleTimeoutMs */
    uint64_t connectionsPaused;      /* times a connection stopped being read */
    size_t   connections;            /* open now */
    size_t   pendingRequests;        /* being handled now */
    size_t   duplicateCacheEntries;  /* in the cache now */
    size_t   duplicateCacheBytes;
} oncRPCServerStats;

/* Read or change the limits; changes apply at once. The duplicate cache is
 * emptied when its size or lifetime changes. */
ONCRPC_API void       oncRPCServerGetLimits(const oncRPCServer *srv, oncRPCServerLimits *limits);
ONCRPC_API oncRPCStat oncRPCServerSetLimits(oncRPCServer *srv, const oncRPCServerLimits *limits);
ONCRPC_API void       oncRPCServerGetStats(const oncRPCServer *srv, oncRPCServerStats *stats);
/* Same as setting limits.maxRecord. */
ONCRPC_API void oncRPCServerSetMaxRecord(oncRPCServer *srv, size_t bytes);
/* Number of requests handed to dispatch routines and not yet finished. */
ONCRPC_API size_t oncRPCServerPending(const oncRPCServer *srv);
ONCRPC_API const oncRPCAllocator *oncRPCServerAllocator(const oncRPCServer *srv);

/*
 * Decode the call arguments into newly allocated, zeroed storage of size
 * bytes. Only valid inside the dispatch routine. The arguments belong to
 * the request and are released when it is finished. Returns NULL if the
 * arguments cannot be decoded (answer with oncRPCSvcErrDecode).
 */
ONCRPC_API void *oncRPCSvcDecodeArgs(oncRPCSvcReq *req, oncRPCXdrProc xargs, size_t size);
/* Send a successful reply (res encoded with xres, NULL for void) and finish
 * the request. res is not retained. Returns non-zero if the reply was
 * encoded (if encoding fails, SYSTEM_ERR is sent instead). */
ONCRPC_API int  oncRPCSvcReply(oncRPCSvcReq *req, oncRPCXdrProc xres, const void *res);
/* Finish a request without replying. */
ONCRPC_API void oncRPCSvcDrop(oncRPCSvcReq *req);
/* Format the caller's address as "host:port". Returns 0 on success. */
ONCRPC_API int  oncRPCSvcPeer(const oncRPCSvcReq *req, char *buf, size_t len);

/* Error replies; each finishes the request. */
ONCRPC_API void oncRPCSvcErrNoProc(oncRPCSvcReq *req);     /* PROC_UNAVAIL */
ONCRPC_API void oncRPCSvcErrDecode(oncRPCSvcReq *req);     /* GARBAGE_ARGS */
ONCRPC_API void oncRPCSvcErrSystem(oncRPCSvcReq *req);     /* SYSTEM_ERR */
ONCRPC_API void oncRPCSvcErrNoProg(oncRPCSvcReq *req);     /* PROG_UNAVAIL */
ONCRPC_API void oncRPCSvcErrProgVers(oncRPCSvcReq *req, uint32_t low, uint32_t high);
ONCRPC_API void oncRPCSvcErrAuth(oncRPCSvcReq *req, oncRPCAuthStat why);

#ifdef __cplusplus
}
#endif

#endif /* ONCRPC_H */
