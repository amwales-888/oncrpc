# Using liboncrpc

liboncrpc is a clean-room C99 implementation of:

- **XDR**: External Data Representation, RFC 4506
- **ONC RPC version 2**: RFC 5531, over TCP (record marking) and UDP
- **Port mapper version 2**: RFC 1833 (lookup and registration)
- **The XDR/RPC language**: `.x` files, with a parser and the `oncrpcgen`
  code generator for client stubs, server dispatch and XDR routines

Design rules:

- **Asynchronous, on your libuv loop.** Nothing blocks. Operations start
  work and complete through callbacks, which always run from the loop and
  never from inside the function that started them. liboncrpc
  shares the loop with whatever else your application runs on it.
- **Your allocator.** The library never calls malloc/realloc/free. All
  memory comes from an `oncRPCAllocator` you supply (`check-alloc` in the
  makefiles verifies this).
- **Naming.** Public functions and types are `oncRPC` + CamelCase
  (`oncRPCClientCreate`, `oncRPCXdrInt32`); constants and macros are
  `ONCRPC_` + upper case.

| Header    | Contents                                                     |
|-----------|--------------------------------------------------------------|
| `oncrpc.h`| RPC client, server, port mapper; includes `xdr.h` and `<uv.h>` |
| `xdr.h`   | allocator, XDR streams and routines (no libuv dependency)    |
| `xspec.h` | `.x` parser and code generator API                           |

Contents:

1. [Workflow](#1-workflow)
2. [The allocator](#2-the-allocator)
3. [Writing a .x file](#3-writing-a-x-file)
4. [oncrpcgen](#4-oncrpcgen)
5. [Generated code](#5-generated-code)
6. [Writing a client](#6-writing-a-client)
7. [Writing a server](#7-writing-a-server)
8. [Memory and lifetime rules](#8-memory-and-lifetime-rules)
9. [XDR API](#9-xdr-api)
10. [RPC API reference](#10-rpc-api-reference)
11. [Parser and generator API](#11-parser-and-generator-api)
12. [Limits and conformance notes](#12-limits-and-conformance-notes)

---

## 1. Workflow

```
  myproto.x ──oncrpcgen──► myproto.h        types, constants, prototypes
                           myproto_xdr.c    XDR routines      (client + server)
                           myproto_clnt.c   client stubs      (client)
                           myproto_svc.c    dispatch, replies (server)
                           [myproto_server_skel.c  -S: template to fill in]
                           [myproto_client_skel.c  -C: example client]
```

```sh
oncrpcgen myproto.x
cc -std=c99 -o myclient myclient.c myproto_xdr.c myproto_clnt.c -loncrpc -luv
cc -std=c99 -o myserver myserver.c myproto_xdr.c myproto_svc.c -loncrpc -luv
# On Linux, add -D_POSIX_C_SOURCE=200809L for your own files under strict
# -std=c99 (see BUILDING.md). With Visual Studio, see BUILDING.md.
```

`examples/` holds eight small, complete examples, starting with a client
asking a server to add two numbers. See `examples/README.md`; building the
library (`BUILDING.md`) builds them too.

## 2. The allocator

```c
typedef struct oncRPCAllocator {
    void *(*realloc)(void *ctx, void *ptr, size_t size);
    void  (*free)(void *ctx, void *ptr);
    void   *ctx;
} oncRPCAllocator;
```

- `realloc` behaves like C `realloc`: `ptr` may be NULL; `size` is never 0;
  return NULL on failure, leaving `ptr` valid.
- `free` is never called with NULL.
- `ctx` is passed to both, for arenas, pools, accounting and so on.
- The structure must stay valid and unchanged for as long as anything created
  with it exists. Typically it is a `static const` object.

The simplest allocator:

```c
static void *app_realloc(void *ctx, void *p, size_t n) { (void)ctx; return realloc(p, n); }
static void  app_free(void *ctx, void *p)               { (void)ctx; free(p); }
static const oncRPCAllocator app_alloc = { app_realloc, app_free, NULL };
```

Every object that allocates takes the allocator: clients, servers, XDR
decode/free streams and parsed specifications. Helpers that use it:
`oncRPCAlloc` (zeroed), `oncRPCAllocArray`, `oncRPCRealloc`, `oncRPCFree`,
`oncRPCStrdup`.

The library handles allocation failure everywhere: the operation fails
with `ONCRPC_ERR_NOMEM` or the affected call or reply fails, and nothing leaks.
`test_rpc` checks this by failing each allocation in turn.

libuv's own internal allocations (for example inside `uv_getaddrinfo`) are
libuv's business; see `uv_replace_allocator` if you need to control those too.

## 3. Writing a .x file

The language is that of RFC 4506 section 6 plus the program definitions of
RFC 5531 section 12.

```c
const MAX_NAME = 64;                       /* constants  -> #define        */

enum color { RED = 0, GREEN = 1, BLUE };   /* missing values count up      */

struct point { float x; float y; };

union result switch (int status) {         /* discriminated union          */
case 0:  hyper value;
case 1:  string error<MAX_NAME>;
default: void;
};

typedef point  path<>;                     /* variable-length array        */
typedef opaque digest[16];                 /* fixed-length opaque          */

struct node { string name<>; node *next; };/* optional data / linked list  */

program SHAPES_PROG {
    version SHAPES_V1 {
        void   PING(void)             = 0;
        result MEASURE(path)          = 1;
        string NAME(color)            = 2;
        int    ADD(int, int)          = 3; /* several arguments            */
    } = 1;
} = 0x20000042;
```

### Types and their C mapping

| .x type                     | C type                                        |
|-----------------------------|-----------------------------------------------|
| `int`, `long`               | `int32_t`                                     |
| `unsigned int`, `unsigned`  | `uint32_t`                                    |
| `hyper`                     | `int64_t`                                     |
| `unsigned hyper`            | `uint64_t`                                    |
| `float`, `double`           | `float`, `double`                             |
| `bool`                      | `oncRPCBool` (`int32_t`, 0 or 1)              |
| `char` / `unsigned char`    | `int8_t` / `uint8_t` (sent as a 32-bit int)   |
| `short` / `unsigned short`  | `int16_t` / `uint16_t` (sent as a 32-bit int) |
| `enum e {...}`              | `typedef enum e {...} e;`                     |
| `struct s {...}`            | `struct s {...}` with `typedef struct s s;`   |
| `union u switch (T d) {...}`| `struct u { T d; union {...} u_u; }`          |
| `quadruple`                 | rejected (no portable C type)                 |

### Declarations and their C mapping

| .x declaration        | C member                                                  |
|-----------------------|-----------------------------------------------------------|
| `T x;`                | `T x;`                                                    |
| `T x[N];`             | `T x[N];`                                                 |
| `T x<N>;` / `T x<>;`  | `struct { uint32_t x_len; T *x_val; } x;`                 |
| `opaque x[N];`        | `uint8_t x[N];`                                           |
| `opaque x<N>;`        | `struct { uint32_t x_len; uint8_t *x_val; } x;`           |
| `string x<N>;`        | `char *x;` (NUL-terminated)                               |
| `T *x;`               | `T *x;` (NULL = absent)                                   |

For a `typedef` the member names use the typedef name, e.g.
`typedef double value_list<>;` gives `value_list_len` and `value_list_val`.
A union's arms live in `<union>_u`, e.g. `res.result_u.value`. Case labels
may be numbers, constants or enum items; `TRUE`/`FALSE` work for `bool`
discriminants. Without `default:`, an unknown discriminant is a decoding error.

### Extensions

- `%` at the start of a line: the rest of the line goes into the generated header.
- `#` at the start of a line is ignored; `oncrpcgen` does **not** run the C
  preprocessor (use `cc -E -P -x c` first if your `.x` files need it).
- `//` and `/* */` comments.
- `string` as a procedure argument or result type.
- Inline type definitions get names automatically: `typedef struct {...} name;`
  defines `struct name`, and an anonymous `struct {...} field;` inside
  `struct s` becomes the type `s_field`.
- `struct name` / `union name` / `enum name` may be used to refer to a type.

## 4. oncrpcgen

```
usage: oncrpcgen [options] file.x

  -a        header, XDR routines, client stubs and server dispatch (default)
  -h        header                      <base>.h
  -c        XDR routines                <base>_xdr.c
  -l        client stubs                <base>_clnt.c
  -s        server dispatch and replies <base>_svc.c
  -S        server template with main() <base>_server_skel.c
  -C        example client with main()  <base>_client_skel.c
  -o DIR    output directory (default: current directory)
  -O FILE   single output to FILE ('-' = stdout); needs exactly one of -h/-c/-l/-s/-S/-C
  -b NAME   base name (default: input name without directory and ".x")
  -f        allow overwriting existing skeleton files
  --help, --version
```

Skeleton files are templates you edit, so they are never overwritten unless
`-f` is given. Errors are reported as `file:line: error: message` with exit
status 1.

## 5. Generated code

For `program P { version V { R X(A) = n; } = v; } = p;` the header contains
(procedure and program names lower-cased, `v` the version number):

```c
#define P p
#define V v
#define X n

/* client stub: starts the call and returns at once */
oncRPCStat x_v(oncRPCClient *clnt, const A *arg, R *res, oncRPCCallCb cb, void *data);

/* server procedure you implement; finish req now or later */
void x_v_svc(const A *arg, oncRPCSvcReq *req);

/* reply helper: encodes *res (not retained) and finishes req */
int x_v_reply(oncRPCSvcReq *req, const R *res);

/* serve this version on a server */
oncRPCStat p_v_register(oncRPCServer *srv, void *ctx);
```

- `void` arguments or results are left out of the parameter lists.
- `string` arguments and results are `const char *` in stubs, procedures and
  replies; the client receives a string result as `char **res`.
- With several arguments, `X(A, B)`, they are passed separately
  (`const A *arg1, const B *arg2`) and a struct `x_v_args` carries them on
  the wire.
- Procedure 0 is answered automatically unless the `.x` file defines it.

For every type `T`:

```c
int  xdr_T(oncRPCXdr *xdrs, T *objp);                        /* encode / decode / free */
void xdr_free_T(const oncRPCAllocator *alloc, T *objp);      /* release decoded data   */
```

Generated identifiers are derived from your `.x` names, so they live in your
namespace rather than the library's.

## 6. Writing a client

This is `examples/01_add/client.c` without the argument handling. The
interface is `int ADD(int, int)` in program `ADD_PROG`:

```c
#include "add.h"

static int32_t sum;                 /* must stay valid until the callback */

static void on_add_done(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;                   /* == &sum */
    (void)data;
    if (status == ONCRPC_OK)
        printf("2 + 3 = %d\n", (int)sum);
    else
        printf("failed: %s\n", oncRPCStrError(status));
    oncRPCClientClose(client, NULL, NULL);
}

int main(void)
{
    uv_loop_t *loop = uv_default_loop();
    oncRPCClient *client;
    int32_t a = 2, b = 3;

    /* port 0 would ask the host's port mapper instead */
    if (oncRPCClientCreate(loop, &app_alloc, "127.0.0.1", 5001, ADD_PROG, ADD_VERS,
                           ONCRPC_PROTO_TCP, NULL, NULL, &client) != ONCRPC_OK)
        return 1;

    /* Calls may be made immediately; they are queued until connected.
     * a and b are encoded before add_1 returns. */
    add_1(client, &a, &b, &sum, on_add_done, NULL);

    uv_run(loop, UV_RUN_DEFAULT);   /* returns once the client is closed */
    return 0;
}
```

For results that contain allocated data (strings, arrays, lists), release
them with the generated `xdr_free_<type>(alloc, &result)` once you are done
(see `examples/02_strings` to `04_lists`).

How clients behave:

- **Connection.** `oncRPCClientCreate` returns at once. Port lookup, name
  resolution and connection happen in the background; the optional ready
  callback reports the outcome. Calls made in the meantime are queued.
- **Concurrency.** Any number of calls may be outstanding on one client;
  replies are matched by transaction id and may arrive in any order.
- **Timeouts.** Each call has a total deadline (`oncRPCClientSetTimeout`,
  default 25 s) counted from `oncRPCClientCall`. UDP requests are resent
  every `oncRPCClientSetRetry` interval (default 1 s).
- **TCP connection loss.** Calls already sent fail with
  `ONCRPC_ERR_CANTRECV`. The next call reconnects.
- **Errors.** Server-reported errors arrive as call statuses.
  `oncRPCClientLastError(clnt)` inside the callback gives the version range for
  `ONCRPC_ERR_PROGVERSMISMATCH`/`ONCRPC_ERR_VERSMISMATCH`, the `oncRPCAuthStat`
  for `ONCRPC_ERR_AUTHERROR`, and the libuv error behind transport failures.
- **Closing.** `oncRPCClientClose` completes outstanding calls with
  `ONCRPC_ERR_CANCELLED`, then runs its callback.
- **Loop lifetime.** A client keeps the loop alive only while it has calls
  outstanding (an idle connection is `uv_unref`'d).
- **Raw calls.** Calls without generated stubs:
  `oncRPCClientCall(clnt, proc, xargs, &args, xres, &res, cb, data)`.
- **Threads.** Handles are not thread-safe; use them on their loop's thread.

## 7. Writing a server

Implement the `*_svc` functions and answer with the generated `*_reply`
functions, either immediately (`examples/01_add/server.c`) or later
(`examples/05_async/server.c`):

```c
void add_1_svc(const int32_t *a, const int32_t *b, oncRPCSvcReq *req)
{
    int32_t sum = *a + *b;
    add_1_reply(req, &sum);                 /* immediate reply */
}

void delay_1_svc(const uint32_t *ms, oncRPCSvcReq *req)
{
    /* Deferred reply: keep req (the arguments stay valid too) and answer
     * later - here from a uv_timer callback that calls
     * delay_1_reply(req, &value). */
    start_timer(req, *ms);
}

int main(void)
{
    uv_loop_t *loop = uv_default_loop();
    oncRPCServer *server;

    oncRPCServerCreate(loop, &app_alloc, &server);
    oncRPCServerListen(server, ONCRPC_PROTO_TCP, NULL, 5001);   /* NULL = 0.0.0.0; "::" for IPv6 */
    oncRPCServerListen(server, ONCRPC_PROTO_UDP, NULL, 5001);
    add_prog_1_register(server, NULL);
    /* optional: oncRPCServerPmapRegister(server, ADD_PROG, ADD_VERS, cb, data); */
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
```

How servers behave:

- **Finishing requests.** Every request must be finished exactly once, by a
  `*_reply` function, `oncRPCSvcReply`, an `oncRPCSvcErr*` function or
  `oncRPCSvcDrop` (no reply). Until then, the request and its decoded
  arguments stay valid, so replies can be as asynchronous as you like.
- **Request details.** `req` describes the call: `req->proc`, `req->vers`,
  `req->proto`, `req->credFlavor`, `req->authSys` (machine name, uid, gid
  and groups for AUTH_SYS) and `req->ctx` (the pointer given at registration).
  `oncRPCSvcPeer(req, buf, len)` gives the caller's address.
- **Rejecting callers.** `oncRPCSvcErrAuth(req, ONCRPC_AUTH_TOOWEAK)`
  rejects the credentials.
- **Automatic errors.** Unknown programs, versions and procedures,
  undecodable arguments, bad credentials and RPC version mismatches are
  answered with the proper RFC 5531 error replies.
- **Port mapper.** `oncRPCServerPmapRegister` registers every listener for a
  program version with the local port mapper (call it after
  `oncRPCServerListen`). `oncRPCServerPmapUnregister` removes the
  registration; call it before closing, as `examples/06_portmap/server.c` does.
- **Closing.** `oncRPCServerClose` stops listening and closes connections.
  Its callback runs once all handles are closed **and** all outstanding
  requests have been finished (their replies are discarded).
  `oncRPCServerPending(srv)` tells how many are left.
- **Lost connections.** If a TCP connection drops while a request is
  outstanding, the request stays valid; its reply is discarded.
- **Resent UDP requests.** A UDP client that hears nothing resends its
  request. The server remembers recent UDP requests and their replies (the
  duplicate-request cache), so a resend does not run the procedure twice:
  while the original is still being handled the resend is ignored, and once
  it has been answered the resend gets the same reply again. A request
  finished with `oncRPCSvcDrop` is forgotten, so a resend runs it afresh.
  Resends are recognised by transaction id, caller address and the exact
  request bytes. See "Server limits" below for sizing.
- **Limits.** The server protects itself from busy or hostile clients with
  limits on connections, outstanding requests and idle time; see below.

### Server limits

`oncRPCServerGetLimits` / `oncRPCServerSetLimits` read and change the
limits at any time; `oncRPCServerGetStats` reports what they have done.
For counts and times, 0 means "no limit".

| Field | Default | Effect |
|---|---|---|
| `maxConnections` | 1024 | TCP connections open at once; further ones are accepted and closed at once |
| `maxRequests` | 4096 | requests being handled at once over all transports; at the limit, TCP connections stop being read and new UDP requests are dropped (the clients resend) |
| `maxRequestsPerConnection` | 64 | requests being handled at once for one TCP connection; at the limit, that connection stops being read |
| `idleTimeoutMs` | 300000 | close TCP connections with no traffic and no outstanding request for this long |
| `maxRecord` | 16 MiB | largest TCP request (must not be 0); `oncRPCServerSetMaxRecord` sets only this |
| `duplicateCacheEntries` | 1024 | UDP requests remembered; 0 turns the cache off |
| `duplicateCacheBytes` | 4 MiB | reply bytes kept; 0 = no byte limit |
| `duplicateCacheTtlMs` | 120000 | how long a reply is kept; 0 = until evicted |

```c
oncRPCServerLimits lim;
oncRPCServerGetLimits(server, &lim);
lim.maxRequestsPerConnection = 8;     /* one slow client cannot hog us */
lim.idleTimeoutMs = 60000;
oncRPCServerSetLimits(server, &lim);
```

Notes:

- A connection that stops being read is not lost: its data waits in the
  kernel and the connection carries on as soon as requests finish (TCP flow
  control slows the client down meanwhile). `connectionsPaused` counts these
  pauses.
- The cache is emptied when any of its three settings changes. Requests
  still being handled are never evicted; when the cache holds nothing else,
  new requests are simply not cached.
- Keep `duplicateCacheTtlMs` longer than your clients' total timeout so
  that late resends still find the reply.
- An idle connection can be closed just as a client sends a new call on it;
  that call fails with `ONCRPC_ERR_CANTRECV` and the client reconnects for
  its next one. Choose an idle timeout well above the gaps between your
  clients' calls.
- The limits and the cache use the server's allocator and a housekeeping
  timer that does not keep the loop alive by itself.

### Dispatch without generated code

```c
static int xdr_u32(oncRPCXdr *x, void *p) { return oncRPCXdrUint32(x, (uint32_t *)p); }

static void dispatch(oncRPCSvcReq *req)
{
    uint32_t *in, out;
    switch (req->proc) {
    case 0:
        oncRPCSvcReply(req, NULL, NULL);
        break;
    case 1:
        in = (uint32_t *)oncRPCSvcDecodeArgs(req, xdr_u32, sizeof(*in));
        if (in == NULL) {
            oncRPCSvcErrDecode(req);
            break;
        }
        out = *in * 2;
        oncRPCSvcReply(req, xdr_u32, &out);
        break;
    default:
        oncRPCSvcErrNoProc(req);
    }
}
/* oncRPCServerRegister(srv, 0x20000001, 1, dispatch, NULL); */
```

`oncRPCSvcDecodeArgs` may only be called inside the dispatch routine. The
decoded arguments belong to the request.

## 8. Memory and lifetime rules

| What | Rule |
|---|---|
| Call arguments | Encoded before the stub / `oncRPCClientCall` returns: stack data is fine. |
| Call results | Your storage, zero-initialised (stubs zero it), valid until the callback. On `ONCRPC_OK`, release decoded contents with `xdr_free_T(alloc, res)` (or `oncRPCFree` for a string). On failure, any partial decode has already been freed. |
| Server arguments | Owned by the request; valid until the request is finished. |
| Server results | Encoded by the reply call and not retained: stack data and string literals are fine. |
| Handles | Closed asynchronously; memory is released just before the close callback runs. |
| Allocator | Must outlive every object created with it. |

## 9. XDR API

The XDR layer (`xdr.h`) has no libuv dependency and can be used on its own,
for example for file formats.

```c
uint8_t buf[256];
oncRPCXdr x;
int32_t n = 42;
char *s = "hi", *s2 = NULL;

oncRPCXdrInitEncode(&x, buf, sizeof(buf));        /* or oncRPCXdrInitEncodeDynamic(&x, &app_alloc, 0) */
oncRPCXdrInt32(&x, &n);
oncRPCXdrString(&x, &s, 100);

oncRPCXdrInitDecode(&x, &app_alloc, buf, oncRPCXdrLength(&x));
oncRPCXdrInt32(&x, &n);
oncRPCXdrString(&x, &s2, 100);                    /* allocates s2 from app_alloc */
oncRPCFree(&app_alloc, s2);
```

| Function                                         | Purpose                                     |
|--------------------------------------------------|---------------------------------------------|
| `oncRPCXdrInitEncode(x, buf, cap)`               | encode into a fixed buffer                  |
| `oncRPCXdrInitEncodeDynamic(x, alloc, initial)`  | encode into a growing buffer (`oncRPCXdrDestroy` / `oncRPCXdrDetach`) |
| `oncRPCXdrInitDecode(x, alloc, buf, len)`        | decode from a buffer                        |
| `oncRPCXdrInitFree(x, alloc)`                    | stream that frees decoded data              |
| `oncRPCXdrGetPos/SetPos/Reset/Remaining/Length/Data` | stream position and contents            |
| `oncRPCXdrFree(alloc, proc, obj)`                | free decoded data using routine `proc`      |
| `oncRPCXdrInt32/Uint32/Int64/Uint64`             | `int`, `unsigned int`, `hyper`, `unsigned hyper` |
| `oncRPCXdrInt8/Uint8/Int16/Uint16`               | small integers (32 bits on the wire)        |
| `oncRPCXdrFloat/Double/Bool/Enum/Void`           |                                             |
| `oncRPCXdrOpaque(x, data, len)`                  | `opaque[len]`                               |
| `oncRPCXdrBytes(x, &data, &len, max)`            | `opaque<max>`                               |
| `oncRPCXdrString(x, &s, max)`                    | `string<max>`                               |
| `oncRPCXdrArray(x, &arr, &len, max, elsize, proc)` | `T<max>` with a generic element routine   |
| `oncRPCXdrVector(x, arr, count, elsize, proc)`   | `T[count]`                                  |
| `oncRPCXdrPointer(x, &ptr, size, proc)`          | `T *` (optional data)                       |
| `oncRPCXdrArrayLen / ArrayAlloc / MemFree`       | building blocks used by generated code      |
| `oncRPCXdrAuthSys(x, params)`                    | AUTH_SYS credential body                    |

All routines return non-zero on success. After a failure the stream's
`error` flag stays set and later operations fail, so a chain of calls can be
checked once at the end. Use `ONCRPC_XDR_UNBOUNDED` as the maximum for `<>`.

## 10. RPC API reference

### Client

| Function | Description |
|---|---|
| `oncRPCClientCreate(loop, alloc, host, port, prog, vers, proto, readyCb, data, &clnt)` | start connecting; `port` 0 uses the port mapper |
| `oncRPCClientClose(clnt, cb, data)` | cancel outstanding calls, close |
| `oncRPCClientCall(clnt, proc, xargs, args, xres, res, cb, data)` | start a call |
| `oncRPCClientSetTimeout(clnt, ms)` | total time per call (default 25000) |
| `oncRPCClientSetRetry(clnt, ms)` | UDP retransmit interval (default 1000) |
| `oncRPCClientSetMaxRecord(clnt, bytes)` | largest TCP reply accepted (default 16 MiB) |
| `oncRPCClientSetAuthNone(clnt)` / `SetAuthSys(...)` / `SetAuthSysDefault(clnt)` | credentials |
| `oncRPCClientLastError(clnt)` | details of the call being reported |
| `oncRPCClientData(clnt)` / `oncRPCClientAllocator(clnt)` / `oncRPCClientPending(clnt)` | accessors |

### Port mapper

| Function | Description |
|---|---|
| `oncRPCPmapGetPort(loop, alloc, host, prog, vers, proto, cb, data)` | look up a port; `cb(status, port, data)` |
| `oncRPCPmapSet(loop, alloc, prog, vers, proto, port, cb, data)` | register with the local port mapper |
| `oncRPCPmapUnset(loop, alloc, prog, vers, cb, data)` | unregister |

### Server

| Function | Description |
|---|---|
| `oncRPCServerCreate(loop, alloc, &srv)` / `oncRPCServerClose(srv, cb, data)` | |
| `oncRPCServerListen(srv, proto, ip, port)` | add a TCP or UDP listener (numeric address) |
| `oncRPCServerPort(srv, proto)` | port actually bound |
| `oncRPCServerRegister(srv, prog, vers, dispatch, ctx)` / `oncRPCServerUnregister` | serve a program version |
| `oncRPCServerPmapRegister(srv, prog, vers, cb, data)` / `oncRPCServerPmapUnregister` | port mapper registration |
| `oncRPCServerGetLimits(srv, &lim)` / `oncRPCServerSetLimits(srv, &lim)` | server limits and duplicate-request cache (section 7) |
| `oncRPCServerGetStats(srv, &stats)` | counters: requests, duplicates, refused/idle/paused connections, cache use |
| `oncRPCServerSetMaxRecord(srv, bytes)` | largest TCP request accepted (default 16 MiB) |
| `oncRPCServerPending(srv)` / `oncRPCServerAllocator(srv)` | accessors |
| `oncRPCSvcDecodeArgs(req, xargs, size)` | decode arguments (dispatch only) |
| `oncRPCSvcReply(req, xres, res)` / `oncRPCSvcDrop(req)` | finish a request |
| `oncRPCSvcErrNoProc / ErrDecode / ErrSystem / ErrNoProg / ErrProgVers / ErrAuth` | error replies |
| `oncRPCSvcPeer(req, buf, len)` | caller address |

### Status codes (`oncRPCStat`)

`ONCRPC_OK`, `ONCRPC_ERR_CANTENCODE`, `_CANTDECODE`, `_CANTSEND`,
`_CANTRECV`, `_TIMEDOUT`, `_VERSMISMATCH`, `_AUTHERROR`, `_PROGUNAVAIL`,
`_PROGVERSMISMATCH`, `_PROCUNAVAIL`, `_CANTDECODEARGS`, `_SYSTEMERROR`,
`_UNKNOWNHOST`, `_CANTCONNECT`, `_PMAPFAILURE`, `_PROGNOTREGISTERED`,
`_NOMEM`, `_INVALID`, `_CANCELLED`, `_FAILED`. `oncRPCStrError()` gives text.

A function that starts an operation returns `ONCRPC_OK` if it started, in
which case its callback will run exactly once. Any other return value means
it did not start and no callback will follow.

## 11. Parser and generator API

`xspec.h` exposes the `.x` front end for other tools (documentation
generators, bindings for other languages, validators). It is pure
computation: it neither blocks nor touches files.

```c
char err[256], *text;
size_t len;
oncRPCXSpec *spec = oncRPCXSpecParse(&app_alloc, source_text, "proto.x", err, sizeof(err));
if (spec == NULL) { fprintf(stderr, "%s\n", err); return 1; }

for (const oncRPCXDef *d = spec->defs; d; d = d->next)
    if (d->kind == ONCRPC_XDEF_STRUCT)
        printf("struct %s with %zu fields\n", d->name, d->nfields);

int64_t v;
oncRPCXSpecEval(spec, "MAX_NAME", &v);            /* resolve constants/enum items */

if (oncRPCXSpecGenerate(spec, ONCRPC_GEN_HEADER, "proto", &text, &len, err, sizeof(err)) == 0) {
    fwrite(text, 1, len, stdout);
    oncRPCFree(&app_alloc, text);
}
oncRPCXSpecFree(spec);
```

## 12. Limits and conformance notes

- **Authentication.** AUTH_NONE and AUTH_SYS on both sides. Servers reject
  other flavors with `AUTH_REJECTEDCRED`. Reply verifiers are AUTH_NONE.
- **Transports.** TCP (RFC 5531 record marking; any number of fragments on
  receive, one per message on send) and UDP (64 KiB datagrams), IPv4 and IPv6.
- **At-most-once over UDP.** The duplicate-request cache gives at-most-once
  execution for resends that arrive while their entry is cached; a resend
  arriving after the reply has been evicted or expired runs the procedure
  again. Make procedures idempotent where that matters.
- **Port mapper.** The client speaks protocol version 2 over UDP, which
  current `rpcbind` implementations support alongside versions 3 and 4.
- **Hostile input.** Lengths are checked against declared bounds and the
  bytes actually available before allocating, records are size-limited, and
  booleans and small integers are range-checked.
- **Unsupported.** `quadruple` is rejected. Enum values are not checked
  against the declared items when decoding.
- **Preprocessor.** `oncrpcgen` does not run the C preprocessor (see section 3).
