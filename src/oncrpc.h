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

#define ONCRPC_VERSION_STRING     "4.1.0"
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

/* Port mapper (RFC 1833, version 2). rpcbind is its successor; see below. */
#define ONCRPC_PMAP_PROG          100000u
#define ONCRPC_PMAP_VERS          2u
#define ONCRPC_PMAP_PORT          111u
#define ONCRPC_PMAPPROC_NULL      0u
#define ONCRPC_PMAPPROC_SET       1u
#define ONCRPC_PMAPPROC_UNSET     2u
#define ONCRPC_PMAPPROC_GETPORT   3u

/* rpcbind (RFC 1833, versions 3 and 4): same program and port. */
#define ONCRPC_RPCB_PROG          100000u
#define ONCRPC_RPCB_VERS          3u
#define ONCRPC_RPCB_VERS4         4u
#define ONCRPC_RPCBPROC_SET       1u
#define ONCRPC_RPCBPROC_UNSET     2u
#define ONCRPC_RPCBPROC_GETADDR   3u

/* Network ids rpcbind uses for TCP and UDP over IPv4 and IPv6. */
#define ONCRPC_NETID_TCP          "tcp"
#define ONCRPC_NETID_UDP          "udp"
#define ONCRPC_NETID_TCP6         "tcp6"
#define ONCRPC_NETID_UDP6         "udp6"
#define ONCRPC_NETID_LOCAL        "local"   /* address: the socket's path */
/* Room for any universal address (or local path), with its terminating NUL. */
#define ONCRPC_UADDR_MAX          256u

/*
 * Transports. TCP and UDP have their IP protocol numbers. LOCAL is a Unix
 * domain socket (a named pipe on Windows), addressed by a path - or a pipe
 * name such as \\.\pipe\name - instead of a host and port, and framed like
 * TCP.
 */
typedef enum oncRPCProto {
    ONCRPC_PROTO_TCP   = 6,
    ONCRPC_PROTO_UDP   = 17,
    ONCRPC_PROTO_LOCAL = 256
} oncRPCProto;

/* Authentication flavors. The library implements NONE and SYS. */
typedef enum oncRPCAuthFlavor {
    ONCRPC_AUTH_NONE  = 0,
    ONCRPC_AUTH_SYS   = 1,
    ONCRPC_AUTH_SHORT = 2,
    ONCRPC_AUTH_DH    = 3,
    ONCRPC_RPCSEC_GSS = 6,
    ONCRPC_AUTH_TLS   = 7    /* RFC 9289: asks a server to start TLS */
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
    ONCRPC_ERR_FAILED,            /* unspecified failure */
    ONCRPC_ERR_TLS                /* TLS refused or failed (see oncRPCError.detail) */
} oncRPCStat;

/* Details of a call's outcome (see oncRPCClientLastError). */
typedef struct oncRPCError {
    oncRPCStat     stat;
    uint32_t       low, high;   /* supported range for *_VERSMISMATCH */
    oncRPCAuthStat why;         /* reason for ONCRPC_ERR_AUTHERROR */
    int            uvError;     /* libuv error code behind a transport failure, or 0 */
    const char    *detail;      /* for ONCRPC_ERR_TLS: what went wrong, or NULL */
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
 * address). If port is 0 the port is looked up with the host's rpcbind:
 * GETADDR in version 4 or 3, asked over the same transport (TCP or UDP) and
 * address family the client will use, or the port mapper's GETPORT
 * (version 2) if the daemon speaks nothing newer. Only the port is taken
 * from the answer.
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

/* value: the port (GetPort) or the port mapper's boolean answer (Set/Unset).
 * The oncRPCPmap* functions speak version 2 only, which knows IPv4 TCP and
 * UDP; the oncRPCRpcb* functions below speak versions 4 and 3. */
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

/* ---- rpcbind --------------------------------------------------------------- */

/*
 * rpcbind versions 3 and 4 describe a service by a network id ("tcp",
 * "udp", "tcp6", "udp6") and a universal address: the IP address as text
 * followed by the port's two bytes, high first, so "192.0.2.7.4.1" and
 * "::1.4.1" both mean port 4 * 256 + 1 = 1025. Unlike version 2 they can
 * describe IPv6 services.
 *
 * These functions ask in version 4 and fall back to version 3 when the
 * daemon does not speak 4. (oncRPCServerPmapRegister and clients created
 * with port 0 also fall back to version 2.)
 */

/* uaddr: the service's universal address (valid during the callback), or
 * NULL on failure; ONCRPC_ERR_PROGNOTREGISTERED if rpcbind knows none.
 *
 * rpcbind answers GETADDR for the transport the question arrives on, not
 * for the netid in it: oncRPCRpcbGetAddr asks over TCP for "tcp"/"tcp6" and
 * over UDP for "udp"/"udp6", and over IPv4 or IPv6 as host resolves (IPv4
 * preferred). To find a tcp6 service, give an IPv6 host such as "::1". */
typedef void (*oncRPCRpcbAddrCb)(oncRPCStat status, const char *uaddr, void *data);

ONCRPC_API oncRPCStat oncRPCRpcbGetAddr(uv_loop_t *loop, const oncRPCAllocator *alloc,
                                        const char *host, uint32_t prog, uint32_t vers,
                                        const char *netid, oncRPCRpcbAddrCb cb, void *data);
/* Register or unregister with rpcbind on the local host; value in the
 * callback is rpcbind's boolean answer. netid NULL or "" in Unset removes
 * the program version for every network id. */
ONCRPC_API oncRPCStat oncRPCRpcbSet(uv_loop_t *loop, const oncRPCAllocator *alloc,
                                    uint32_t prog, uint32_t vers, const char *netid,
                                    const char *uaddr, oncRPCPmapCb cb, void *data);
ONCRPC_API oncRPCStat oncRPCRpcbUnset(uv_loop_t *loop, const oncRPCAllocator *alloc,
                                      uint32_t prog, uint32_t vers, const char *netid,
                                      oncRPCPmapCb cb, void *data);

/* Universal address of an IPv4 or IPv6 socket address, into buf (at least
 * ONCRPC_UADDR_MAX bytes is always enough). Returns 0, or -1 on failure. */
ONCRPC_API int oncRPCUaddrFromSockaddr(const struct sockaddr *addr, char *buf, size_t len);
/* Socket address of a universal address. Returns 0, or -1 if malformed. */
ONCRPC_API int oncRPCUaddrToSockaddr(const char *uaddr, struct sockaddr_storage *addr);
/* Network id for a protocol and address family (AF_INET or AF_INET6). */
ONCRPC_API const char *oncRPCNetid(oncRPCProto proto, int family);

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
 * (0 = any free port). May be called several times. For ONCRPC_PROTO_LOCAL,
 * ip is the socket's path (a pipe name on Windows) and port is ignored; the
 * socket file is removed when the server closes, but one left behind by a
 * program that did not close makes this fail until it is deleted. */
ONCRPC_API oncRPCStat oncRPCServerListen(oncRPCServer *srv, oncRPCProto proto,
                                         const char *ip, uint16_t port);
/* Port of the first listener of the given protocol, 0 if none. */
ONCRPC_API uint16_t oncRPCServerPort(const oncRPCServer *srv, oncRPCProto proto);
/* Serve a program version with dispatch; ctx is passed in each request. */
ONCRPC_API oncRPCStat oncRPCServerRegister(oncRPCServer *srv, uint32_t prog, uint32_t vers,
                                           oncRPCDispatchFn dispatch, void *ctx);
ONCRPC_API oncRPCStat oncRPCServerUnregister(oncRPCServer *srv, uint32_t prog, uint32_t vers);
/* Register every listener for prog/vers with rpcbind on the local host,
 * replacing stale entries. Each listener is registered under its own network
 * id and address, so IPv6 listeners are registered as tcp6/udp6. A daemon
 * that speaks only version 2 (portmap) gets the IPv4 listeners; IPv6 ones
 * then fail with ONCRPC_ERR_PMAPFAILURE. cb (may be NULL) reports the
 * outcome: the first failure, or ONCRPC_OK. Unregister removes the program
 * version for every network id. */
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
    uint64_t tlsSessions;            /* connections upgraded to TLS */
    uint64_t tlsFailures;            /* TLS handshakes that failed */
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
/* Format the caller's address as "host:port" ("local" for a local
 * transport, with the uid and gid when known). Returns 0 on success. */
ONCRPC_API int  oncRPCSvcPeer(const oncRPCSvcReq *req, char *buf, size_t len);
/* The user and group ids of the process that sent a request over a local
 * transport, as the operating system reports them (Linux, macOS and the
 * BSDs; not Windows). Returns 0, or -1 when they are not known. */
ONCRPC_API int  oncRPCSvcLocalCred(const oncRPCSvcReq *req, uint32_t *uid, uint32_t *gid);

/* Error replies; each finishes the request. */
ONCRPC_API void oncRPCSvcErrNoProc(oncRPCSvcReq *req);     /* PROC_UNAVAIL */
ONCRPC_API void oncRPCSvcErrDecode(oncRPCSvcReq *req);     /* GARBAGE_ARGS */
ONCRPC_API void oncRPCSvcErrSystem(oncRPCSvcReq *req);     /* SYSTEM_ERR */
ONCRPC_API void oncRPCSvcErrNoProg(oncRPCSvcReq *req);     /* PROG_UNAVAIL */
ONCRPC_API void oncRPCSvcErrProgVers(oncRPCSvcReq *req, uint32_t low, uint32_t high);
ONCRPC_API void oncRPCSvcErrAuth(oncRPCSvcReq *req, oncRPCAuthStat why);

/* ---- TLS (RFC 9289, RPC-with-TLS) ------------------------------------------ */

/*
 * A TCP connection can be upgraded to TLS 1.3: the client asks with a NULL
 * call carrying the AUTH_TLS credential, a server that agrees answers
 * "STARTTLS", and every later message on the connection is encrypted.
 * Clients still send their usual credentials (AUTH_NONE, AUTH_SYS) inside.
 * TLS is built on OpenSSL 3. Only TCP connections are upgraded: UDP
 * (which would need DTLS) and local sockets are not.
 *
 * A configuration holds certificates, keys and trust for one side and is
 * shared by any number of clients or servers. Certificates and keys are
 * given as PEM text in memory, so that nothing here reads files; read them
 * however suits the application (the examples use plain stdio at start-up).
 */
typedef struct oncRPCTlsConfig oncRPCTlsConfig;

typedef enum oncRPCTlsRole {
    ONCRPC_TLS_CLIENT,
    ONCRPC_TLS_SERVER
} oncRPCTlsRole;

typedef enum oncRPCTlsVerify {
    /* Client: do not check the server's certificate (for testing only).
     * Server: do not ask clients for certificates (the server default). */
    ONCRPC_TLS_VERIFY_NONE,
    /* Client: the server's certificate must chain to a trusted certificate
     * and name the server (the client default). Server: ask clients for a
     * certificate and check any that is given. */
    ONCRPC_TLS_VERIFY_PEER,
    /* Server: clients must present a certificate that checks out (mutual
     * TLS). Same as VERIFY_PEER for a client. */
    ONCRPC_TLS_VERIFY_REQUIRE
} oncRPCTlsVerify;

ONCRPC_API oncRPCStat oncRPCTlsConfigCreate(const oncRPCAllocator *alloc, oncRPCTlsRole role,
                                            oncRPCTlsConfig **out);
/* Release the application's hold; clients and servers using the
 * configuration keep it alive until they close. */
ONCRPC_API void       oncRPCTlsConfigFree(oncRPCTlsConfig *cfg);
/* This side's certificate (followed by any intermediate certificates) and
 * private key, as PEM text. Required for a server; for a client, sent when
 * a server asks. */
ONCRPC_API oncRPCStat oncRPCTlsConfigSetCertificate(oncRPCTlsConfig *cfg, const char *certPem,
                                                    const char *keyPem);
/* Trust the certificate(s) in PEM text as issuers of the peer's
 * certificate. May be called several times. */
ONCRPC_API oncRPCStat oncRPCTlsConfigAddTrusted(oncRPCTlsConfig *cfg, const char *pem);
/* Also trust OpenSSL's default certificate store. OpenSSL may read files
 * from it while checking a certificate, which blocks briefly. */
ONCRPC_API oncRPCStat oncRPCTlsConfigUseSystemTrust(oncRPCTlsConfig *cfg);
ONCRPC_API oncRPCStat oncRPCTlsConfigSetVerify(oncRPCTlsConfig *cfg, oncRPCTlsVerify mode);

/*
 * Use TLS for this client's TCP connections. Call it before the loop next
 * runs after oncRPCClientCreate (the connection is upgraded as soon as it
 * is made, and again whenever it is remade). serverName is the name the
 * server's certificate must carry - a DNS name, or an IP address - and is
 * sent to the server (SNI) when it is a name; NULL means the host given to
 * oncRPCClientCreate. Calls wait until TLS is up; if it cannot be set up,
 * they fail with ONCRPC_ERR_TLS, with the reason in oncRPCClientLastError's
 * detail (and so does the ready callback).
 */
ONCRPC_API oncRPCStat oncRPCClientSetTls(oncRPCClient *clnt, oncRPCTlsConfig *cfg,
                                         const char *serverName);

/*
 * Offer TLS on this server's TCP connections. With required set, requests
 * that did not arrive over TLS - on TCP connections that were not
 * upgraded, and over UDP - are refused with AUTH_ERROR / AUTH_TOOWEAK;
 * local sockets are not affected. cfg must have a certificate.
 */
ONCRPC_API oncRPCStat oncRPCServerSetTls(oncRPCServer *srv, oncRPCTlsConfig *cfg, int required);

/* Whether the request arrived over TLS. */
ONCRPC_API int oncRPCSvcTls(const oncRPCSvcReq *req);
/* The subject of the certificate the client presented and that checked out
 * (mutual TLS), as an RFC 2253 string such as "CN=alice,O=Example". Returns
 * 0, or -1 if there is none. */
ONCRPC_API int oncRPCSvcTlsPeer(const oncRPCSvcReq *req, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ONCRPC_H */
