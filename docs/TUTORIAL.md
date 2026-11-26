# Getting started with liboncrpc

This tutorial builds a small networked service from nothing: a server that
greets people by name, and a client that asks it to. Along the way it
explains how liboncrpc works, and in particular its callbacks, which are the
part most people find new. It finishes by comparing liboncrpc with the
traditional RPC library found on Unix systems, for readers who know that
one.

You need to be comfortable compiling a C program from the command line. You
do not need to know anything about networking or RPC.

## What RPC is

A *remote procedure call* (RPC) lets a program call a function that runs in
another program, usually on another computer. The caller (the *client*)
names the function and passes its arguments; they travel over the network to
the *server*, which runs the function and sends the result back.

```
   client program                              server program
  ----------------                            ----------------
  GREET("Ada")  ---- "GREET", "Ada" ------->  runs GREET("Ada")
                <--- "Hello, Ada!" ---------  returns "Hello, Ada!"
```

ONC RPC, which liboncrpc implements, is one of the oldest and most widely
used RPC systems: NFS, the network file system, is built on it. It has two
parts:

- **XDR** (External Data Representation) decides how numbers, strings and
  structures are turned into bytes, so that computers with different
  processors agree on what was sent.
- **The RPC protocol** decides how a call and its reply are wrapped, how a
  client names the function it wants, how errors come back, and so on.

You describe your service's functions and data in a small language, in a
`.x` file. A tool, `oncrpcgen`, turns that into C code that does all the
XDR and RPC work, so that your program deals only with ordinary C values.

## Before you start

You need a C compiler, libuv (the event library liboncrpc is built on),
OpenSSL 3, and liboncrpc itself. On Debian or Ubuntu:

```sh
sudo apt install build-essential pkg-config libuv1-dev libssl-dev
cd liboncrpc                         # the unpacked source
make -f Makefile-Linux
sudo make -f Makefile-Linux install  # into /usr/local
sudo ldconfig                        # so programs find the shared library
```

This installs the `oncrpcgen` tool, the library, and its headers in
`/usr/local/include/oncrpc`. `BUILDING.md` covers FreeBSD, macOS and
Windows, and installing somewhere else.

Make an empty directory for the tutorial and work in it.

## Step 1: describe the service

Create `hello.x`:

```
/*
 * hello.x - a greeting service.
 */
program HELLO_PROG {
    version HELLO_V1 {
        string       GREET(string) = 1;   /* returns "Hello, <name>!"   */
        unsigned int COUNT(void)   = 2;   /* how many greetings so far */
    } = 1;
} = 0x20001234;
```

Reading it from the outside in:

- `program HELLO_PROG { ... } = 0x20001234;` declares a program and its
  number. The number is how a client says which service it wants; numbers
  from `0x20000000` to `0x3fffffff` are free for anyone to use.
- `version HELLO_V1 { ... } = 1;` declares version 1 of the program. When a
  service changes, you add a version, so that old clients keep working.
- Each line inside is a procedure: *result type*, *name*, *(argument
  types)*, `=` *procedure number*. `GREET` takes a string and returns one;
  `COUNT` takes nothing (`void`) and returns an unsigned integer.

`.x` files can also define constants, enumerations, structures, unions,
arrays and linked lists; see [oncrpcgen(1)](man/oncrpcgen.1.md).

## Step 2: generate the code

```sh
oncrpcgen hello.x
```

This writes four files:

| File | What it holds | Used by |
|---|---|---|
| `hello.h` | declarations of everything below | both |
| `hello_xdr.c` | code that turns the arguments and results into bytes and back | both |
| `hello_clnt.c` | one function per procedure, to call it | the client |
| `hello_svc.c` | code that receives calls and hands them to your functions | the server |

You never edit these files; regenerate them when `hello.x` changes. The
interesting part of `hello.h` is:

```c
/* client side: start a call */
oncRPCStat oncRPCGreetV1(oncRPCClient *clnt, const char *arg, char **res,
                         oncRPCCallCb cb, void *data);
oncRPCStat oncRPCCountV1(oncRPCClient *clnt, uint32_t *res, oncRPCCallCb cb, void *data);

/* server side: you write these two */
void oncRPCGreetV1Svc(const char *arg, oncRPCSvcReq *req);
void oncRPCCountV1Svc(oncRPCSvcReq *req);

/* server side: send an answer */
int oncRPCGreetV1Reply(oncRPCSvcReq *req, const char *res);
int oncRPCCountV1Reply(oncRPCSvcReq *req, const uint32_t *res);

/* server side: start serving the program */
oncRPCStat oncRPCHelloProgV1Register(oncRPCServer *srv, void *ctx);
```

Every name starts with `oncRPC`, then the `.x` name in CamelCase (`GREET`
becomes `Greet`), then `V1` for the version.

## Step 3: write the server

Create `server.c`:

```c
/* server.c - the greeting service. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "hello.h"

/* 1. Memory: liboncrpc asks the program for every byte it uses. */
static void *my_realloc(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    return realloc(ptr, size);
}

static void my_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static const oncRPCAllocator my_alloc = { my_realloc, my_free, NULL };

static uint32_t greetings;   /* how many GREETs so far */

/* 2. The procedures. The generated code calls these for each request. */
void oncRPCGreetV1Svc(const char *name, oncRPCSvcReq *req)
{
    char text[100];

    greetings++;
    snprintf(text, sizeof(text), "Hello, %s!", name);
    printf("server: greeting %s\n", name);
    oncRPCGreetV1Reply(req, text);   /* send the answer */
}

void oncRPCCountV1Svc(oncRPCSvcReq *req)
{
    oncRPCCountV1Reply(req, &greetings);
}

/* 3. Start the server and run the event loop. */
int main(void)
{
    uv_loop_t *loop = uv_default_loop();
    oncRPCServer *server;

    signal(SIGPIPE, SIG_IGN);   /* a client hanging up must not stop us */

    if (oncRPCServerCreate(loop, &my_alloc, &server) != ONCRPC_OK ||
        oncRPCServerListen(server, ONCRPC_PROTO_TCP, "127.0.0.1", 5555) != ONCRPC_OK ||
        oncRPCHelloProgV1Register(server, NULL) != ONCRPC_OK) {
        fprintf(stderr, "server: cannot start\n");
        return 1;
    }
    printf("server: waiting for calls on port 5555\n");
    fflush(stdout);
    uv_run(loop, UV_RUN_DEFAULT);   /* runs until the program is stopped */
    return 0;
}
```

There are three parts.

**1. Memory.** liboncrpc never calls `malloc()` or `free()` itself.
Instead you give it an *allocator*: a structure with two functions it calls
whenever it needs memory or is done with it. Here they simply pass the work
on to the C library. A program with special needs (a memory pool, a limit,
statistics) can do something else, without changing the library.

**2. The procedures.** You write one function per procedure, with the names
from `hello.h`. The generated code calls `oncRPCGreetV1Svc()` each time a
client calls `GREET`, with the decoded argument (`name`) and a *request*
(`req`) that stands for this particular call. The function answers by
calling `oncRPCGreetV1Reply()`, which encodes the result and sends it. The
text is a local array, which is fine: the reply function has finished with
it by the time it returns.

Every request must be answered exactly once. Usually you answer straight
away, as here, but you may also keep `req` and answer later, which section
"Callbacks on the server" below comes back to.

**3. `main()`.** The server is created on an *event loop* (more about that
below), told to listen for TCP connections on port 5555 of this computer,
and given the `HELLO_PROG` version 1 functions with the generated
`oncRPCHelloProgV1Register()`. Then `uv_run()` runs the loop, which waits
for calls and hands them to your functions, until the program is stopped.

`signal(SIGPIPE, SIG_IGN)` stops the operating system from killing the
program if a client disconnects while the server is writing to it.

## Step 4: write the client

Create `client.c`:

```c
/* client.c - calls the greeting service. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "hello.h"

static void *my_realloc(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    return realloc(ptr, size);
}

static void my_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static const oncRPCAllocator my_alloc = { my_realloc, my_free, NULL };

/* Results are written here when the replies arrive, so they must
 * still exist then: they are global, not local to a function. */
static char *greeting;
static uint32_t count;

static void on_count(oncRPCClient *client, oncRPCStat status, void *result, void *data);

/* Runs when the reply to GREET has arrived (or the call failed). */
static void on_greet(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;   /* the same as &greeting */
    (void)data;

    if (status != ONCRPC_OK) {
        printf("client: GREET failed: %s\n", oncRPCStrError(status));
        oncRPCClientClose(client, NULL, NULL);
        return;
    }
    printf("client: the server says \"%s\"\n", greeting);
    oncRPCFree(&my_alloc, greeting);   /* the string was allocated for us */
    greeting = NULL;

    /* Next step: ask how many greetings there have been. */
    if (oncRPCCountV1(client, &count, on_count, NULL) != ONCRPC_OK)
        oncRPCClientClose(client, NULL, NULL);
}

/* Runs when the reply to COUNT has arrived. */
static void on_count(oncRPCClient *client, oncRPCStat status, void *result, void *data)
{
    (void)result;
    (void)data;

    if (status == ONCRPC_OK)
        printf("client: %u greeting(s) so far\n", (unsigned)count);
    else
        printf("client: COUNT failed: %s\n", oncRPCStrError(status));
    oncRPCClientClose(client, NULL, NULL);   /* all done */
}

int main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    const char *name = argc > 1 ? argv[1] : "world";
    oncRPCClient *client;

    signal(SIGPIPE, SIG_IGN);

    /* Connects in the background: no waiting here. */
    if (oncRPCClientCreate(loop, &my_alloc, "127.0.0.1", 5555, HELLO_PROG, HELLO_V1,
                           ONCRPC_PROTO_TCP, NULL, NULL, &client) != ONCRPC_OK) {
        fprintf(stderr, "client: cannot create the client\n");
        return 1;
    }

    /* Starts the call and returns at once; on_greet runs later. */
    if (oncRPCGreetV1(client, name, &greeting, on_greet, NULL) != ONCRPC_OK)
        oncRPCClientClose(client, NULL, NULL);

    printf("client: call started\n");
    uv_run(loop, UV_RUN_DEFAULT);   /* returns once the client has closed */
    return 0;
}
```

Notice that `main()` does not wait for anything:

1. `oncRPCClientCreate()` creates a client for `HELLO_PROG` version 1 on
   port 5555. It returns at once; connecting happens in the background.
2. `oncRPCGreetV1()` starts the `GREET` call, saying where the result
   should go (`&greeting`) and which function to call when the reply has
   arrived (`on_greet`). It also returns at once, before the call has even
   been sent, which is why "call started" is printed first.
3. `uv_run()` runs the event loop. Inside it, the client connects, sends the
   call and receives the reply, and then calls `on_greet()`.
4. `on_greet()` prints the greeting, frees the string, and starts the next
   call, `COUNT`, with `on_count()` as its callback.
5. `on_count()` prints the count and closes the client. With nothing left
   to do, `uv_run()` returns and the program ends.

## Step 5: build and run

Compile each program with its generated files and link it with liboncrpc
and libuv:

```sh
CFLAGS="-std=c99 -D_POSIX_C_SOURCE=200809L -Wall -I/usr/local/include/oncrpc"
cc $CFLAGS -o server server.c hello_svc.c hello_xdr.c -loncrpc -luv
cc $CFLAGS -o client client.c hello_clnt.c hello_xdr.c -loncrpc -luv
```

(`-D_POSIX_C_SOURCE=200809L` makes the system headers libuv needs visible
under strict C99; `-std=gnu99` does the same.)

Start the server in one terminal:

```
$ ./server
server: waiting for calls on port 5555
```

and run the client in another:

```
$ ./client Ada
client: call started
client: the server says "Hello, Ada!"
client: 1 greeting(s) so far
$ ./client
client: call started
client: the server says "Hello, world!"
client: 2 greeting(s) so far
```

The server prints a line for each greeting. Stop it with Ctrl-C, run the
client again, and it reports the problem instead:

```
client: call started
client: GREET failed: cannot connect
```

That is a working RPC service. The rest of this tutorial explains what
happened.

## How the callbacks work

### The event loop

A program that talks to the network spends most of its time waiting: for a
connection, for a reply, for the next request. The traditional way to wait
is to *block*: a function such as `read()` does not return until data has
arrived, and the program does nothing else meanwhile.

liboncrpc does not block. It is built on libuv's **event loop**, a single
loop, run by `uv_run()`, that waits for whatever happens next on any of the
program's connections and timers, and calls the function that was
registered for that event. Your code consists of short functions that each
react to one event and return quickly.

The consequence is a rule that runs through the whole library:

> A function that starts something returns at once. When the thing has
> finished, a function you supplied - a **callback** - is called, from the
> event loop.

### What happens when the client calls GREET

Following the client program step by step:

```
main()
  oncRPCClientCreate()      creates the client, starts connecting   -> returns
  oncRPCGreetV1()           encodes "Ada", queues the call          -> returns
  printf("call started")
  uv_run()                  the event loop runs:
      ... connection established   -> the queued call is sent
      ... nothing to do, waits
      ... reply arrives            -> "Hello, Ada!" decoded into greeting
      on_greet()                   <- your callback
          oncRPCCountV1()          queues and sends COUNT           -> returns
      ... reply arrives            -> 1 decoded into count
      on_count()                   <- your callback
          oncRPCClientClose()      starts closing                    -> returns
      ... client closed, nothing left
  uv_run() returns
```

### The rules that follow

1. **Code after a call runs before its callback.** `oncRPCGreetV1()`
   returns before the reply exists, always: callbacks are never called
   from inside the function that started the work. This is why "call
   started" is printed before the greeting.

2. **Check the return value, then wait for the callback.** A starting
   function returns `ONCRPC_OK` if the work started; then its callback will
   be called exactly once, with the outcome. Any other value means nothing
   started and no callback will come.

3. **Results must outlive the call.** The arguments are encoded before
   `oncRPCGreetV1()` returns, so they can be local variables. But the
   result is written when the reply arrives, long after the function that
   started the call may have returned. So the result variable must still
   exist then: a global, a `static`, or memory you allocated. This is the
   most common mistake:

   ```c
   void start(oncRPCClient *client)
   {
       char *greeting = NULL;                 /* WRONG: gone when start() returns */
       oncRPCGreetV1(client, "Ada", &greeting, on_greet, NULL);
   }
   ```

4. **To do things in order, start the next step in the callback.** The
   client asks `COUNT` only after `GREET` has been answered by starting it
   inside `on_greet()`. A sequence of calls becomes a chain of callbacks.

5. **To do things at the same time, just start them.** A client can have
   any number of calls outstanding; each callback runs when its own reply
   arrives, in whatever order the replies come. To greet three people at
   once, give each call its own result variable:

   ```c
   #include <stdio.h>
   #include "hello.h"

   static char *greetings[3];   /* one result per call */

   static void on_greeting(oncRPCClient *client, oncRPCStat status, void *result,
                           void *data)
   {
       char **text = result;   /* the res given to this call: &greetings[i] */
       (void)data;
       if (status == ONCRPC_OK) {
           printf("%s\n", *text);
           oncRPCFree(oncRPCClientAllocator(client), *text);
           *text = NULL;
       }
   }

   void greet_three(oncRPCClient *client)
   {
       static const char *names[3] = { "Ada", "Grace", "Alan" };
       int i;

       for (i = 0; i < 3; i++)
           oncRPCGreetV1(client, names[i], &greetings[i], on_greeting, NULL);
   }
   ```

   A callback receives, as `result`, the result pointer its call was given,
   and as `data` the last argument of the call, unchanged. Either can tell
   it which call it is answering.

6. **Free what the library allocated for you.** A string or array result
   is allocated (with your allocator) while the reply is decoded, and is
   yours once the callback has run with `ONCRPC_OK`. `on_greet()` frees
   the greeting with `oncRPCFree()`; structured results are freed with
   `oncRPCXdrFree()`.

7. **Keep callbacks short.** While a callback runs, nothing else in the
   program does: no other reply is read, no server request is handled. Long
   computations belong in a worker thread (for instance `uv_queue_work()`),
   whose completion callback then carries on.

### Callbacks on the server

The server works the same way. `uv_run()` waits for requests and calls
your procedure for each. The procedure gets a request, `req`, and must
finish it exactly once - but not necessarily before it returns. If the
answer needs something slow, such as a database query, a file read, or a
call to another RPC server, the procedure can start that work, keep `req`,
and return. The server goes on handling other clients meanwhile. When the
work's own callback runs, it calls `oncRPCGreetV1Reply(req, ...)`.

If the client disconnects before the answer is ready, the library can tell
you, so that the work can be abandoned; see
[oncRPCSvcSetAbandonCb(3)](man/oncRPCSvcSetAbandonCb.3.md) and
`examples/05_async` and `examples/11_auth` in the source tree.

## How liboncrpc differs from the traditional RPC library

Most Unix systems come with an RPC library (today usually libtirpc, used
through `<rpc/rpc.h>`) and the `rpcgen` tool. liboncrpc speaks the same
protocol, but its programming model is different.

### Calls do not block

The traditional client calls `clnt_call()`, or a stub generated by
`rpcgen` such as `greet_1()`, which sends the call and **waits** until the
reply arrives or the timeout expires:

```c
/* traditional */
char **result = greet_1(&name, clnt);     /* blocks until the answer */
if (result == NULL)
    clnt_perror(clnt, "localhost");
else
    printf("%s\n", *result);
```

That is simple for one call, but a program that needs to do anything else
meanwhile - talk to several servers, serve a user interface, keep a timer
running - needs one thread per outstanding call. With liboncrpc, the call
returns at once and a callback receives the result, so one thread can have
any number of calls outstanding, to any number of servers, alongside
whatever else the program does with libuv.

### The program owns the loop

A traditional server ends `main()` with `svc_run()`, which takes over the
thread for good and dispatches requests until the process exits. A
liboncrpc server runs on the application's own libuv loop, so the same
loop can also run clients, timers, other network protocols, or a
completely different libuv-based library, and `uv_run()` returns when
everything has closed.

### Servers can answer later

A traditional server procedure, such as `greet_1_svc()`, **returns** its
result, by default as a pointer to a `static` variable, and the reply is
sent as soon as it returns. Anything slow it does holds up every other
client. A liboncrpc procedure answers by **calling** a reply function, at
once or later, so slow work does not block the server.

### Other differences

| | Traditional library | liboncrpc |
|---|---|---|
| Starting a client | `clnt_create()` resolves, asks rpcbind and connects before returning | `oncRPCClientCreate()` returns at once; calls wait in a queue until it is connected |
| Waiting for a reply | `clnt_call()` blocks | `oncRPCClientCall()` returns; a callback gets the result |
| Server main loop | `svc_run()`, never returns | the application's `uv_run()` |
| Server procedure | returns a pointer to its (often static) result | calls a reply function, now or later |
| Servers per process | one process-wide table filled by `svc_register()` | any number of independent `oncRPCServer` objects |
| Memory | `malloc()`/`free()` inside the library | the application's allocator, for everything |
| Freeing results | `xdr_free()`, `clnt_freeres()` | `oncRPCXdrFree()`, `oncRPCFree()` |
| Errors | `clnt_perror()`, `clnt_sperror()`, `struct rpc_err` | `oncRPCStrError()`, `oncRPCClientLastError()` |
| Timeout | a `struct timeval` per call | one setting per client (25 s by default) |
| Generated names | `greet_1()`, `greet_1_svc()`, `xdr_foo()` | `oncRPCGreetV1()`, `oncRPCGreetV1Svc()`, `oncRPCXdrFoo()` |
| TLS | not part of the classic API | RPC-with-TLS (RFC 9289) built in |

### Porting a program

When moving code from the traditional library, these are the usual
replacements:

| Traditional | liboncrpc |
|---|---|
| `clnt_create()` | [oncRPCClientCreate(3)](man/oncRPCClientCreate.3.md) |
| `clnt_call()` | [oncRPCClientCall(3)](man/oncRPCClientCall.3.md), or a generated stub |
| `clnt_control(..., CLSET_TIMEOUT, ...)` | [oncRPCClientSetTimeout(3)](man/oncRPCClientSetTimeout.3.md) |
| `clnt_destroy()` | [oncRPCClientClose(3)](man/oncRPCClientClose.3.md) |
| `clnt_perror()`, `clnt_sperror()` | [oncRPCStrError(3)](man/oncRPCStrError.3.md), [oncRPCClientLastError(3)](man/oncRPCClientLastError.3.md) |
| `authsys_create_default()` | [oncRPCClientSetAuthSysDefault(3)](man/oncRPCClientSetAuthSysDefault.3.md) |
| `svctcp_create()`, `svcudp_create()`, `svc_create()` | [oncRPCServerCreate(3)](man/oncRPCServerCreate.3.md) and [oncRPCServerListen(3)](man/oncRPCServerListen.3.md) |
| `svc_register()` | [oncRPCServerRegister(3)](man/oncRPCServerRegister.3.md), or the generated `...Register()` |
| `pmap_set()`, `rpcb_set()` | [oncRPCServerPmapRegister(3)](man/oncRPCServerPmapRegister.3.md) |
| `svc_run()` | `uv_run()` |
| `svc_getargs()` | [oncRPCSvcDecodeArgs(3)](man/oncRPCSvcDecodeArgs.3.md) |
| `svc_sendreply()` | [oncRPCSvcReply(3)](man/oncRPCSvcReply.3.md), or the generated `...Reply()` |
| `svcerr_noproc()`, `svcerr_decode()`, `svcerr_systemerr()` | [oncRPCSvcErrNoProc(3)](man/oncRPCSvcErrNoProc.3.md), [oncRPCSvcErrDecode(3)](man/oncRPCSvcErrDecode.3.md), [oncRPCSvcErrSystem(3)](man/oncRPCSvcErrSystem.3.md) |
| `svcerr_auth()` | [oncRPCSvcErrAuth(3)](man/oncRPCSvcErrAuth.3.md) |
| `pmap_getport()`, `rpcb_getaddr()` | [oncRPCPmapGetPort(3)](man/oncRPCPmapGetPort.3.md), [oncRPCRpcbGetAddr(3)](man/oncRPCRpcbGetAddr.3.md) |
| `xdrmem_create()` | [oncRPCXdrInitEncode(3)](man/oncRPCXdrInitEncode.3.md), [oncRPCXdrInitDecode(3)](man/oncRPCXdrInitDecode.3.md) |
| `xdr_free()` | [oncRPCXdrFree(3)](man/oncRPCXdrFree.3.md) |
| `rpcgen` | [oncrpcgen(1)](man/oncrpcgen.1.md) |

The biggest change is usually not a function but the shape of the code: a
function that called the server and then used the answer becomes two
functions, one that starts the call and a callback that uses the answer.
`.x` files need no changes.

## Common mistakes

- **The result variable is a local** that has gone by the time the reply
  arrives (rule 3). Use a global, a `static`, or allocated memory.
- **Expecting the result straight after the call.** It is only there in
  the callback.
- **Forgetting `uv_run()`.** Nothing happens - no connection, no call -
  until the loop runs.
- **A request that is never answered.** Every server request must get a
  reply, an error reply or a drop, exactly once; otherwise the client waits
  until its timeout, and the server cannot close.
- **Not freeing string and array results**, or freeing them with `free()`
  instead of through the allocator.
- **Blocking in a callback**, for example with `sleep()` or a slow file
  read: the whole program stops meanwhile.
- **Using a client or server from another thread.** They belong to the
  thread that runs their loop.

## Where to go next

- The [manual pages](man/README.md) document every function, each with an
  example.
- `examples/` in the source tree has eleven complete programs, from adding
  two numbers to TLS, local sockets and custom authentication.
- `USAGE.md` describes the library topic by topic, and `BUILDING.md`
  explains building and linking on every platform.
