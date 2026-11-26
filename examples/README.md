# liboncrpc examples

Each directory is a small, complete program pair. Every example introduces
one or two new ideas, so read them in order.

| Example        | Introduces                                                     |
|----------------|----------------------------------------------------------------|
| `01_add/`      | the basics: an interface, a server, a client, one call         |
| `02_strings/`  | string arguments and results; calls one after another          |
| `03_shapes/`   | enums, structs, unions; several calls in flight at once        |
| `04_lists/`    | variable-length arrays and linked lists                        |
| `05_async/`    | replying later from the server, without blocking               |
| `06_portmap/`  | registering with and finding servers through rpcbind; TCP and UDP; clean shutdown |
| `07_errors/`   | AUTH_SYS credentials, rejecting callers, timeouts, unknown procedures and versions |
| `08_xdr/`      | XDR on its own, for files or messages, with no networking      |
| `09_local/`    | a service on a local socket; knowing which user is calling     |
| `10_tls/`      | calls over TLS, with certificates on both sides                 |
| `11_auth/`     | a home-made authentication flavor; noticing when a caller stops waiting |

Building liboncrpc builds them too (see `BUILDING.md`). Where the programs end up:

| Built with | Programs |
|---|---|
| `make -f Makefile-Linux` | `build/linux/release/examples/<example>/server`, `client` (`xdr_demo` for 08_xdr) |
| `make -f Makefile-FreeBSD` | `build/freebsd/release/examples/<example>/...` |
| `make -f Makefile-macOS` | `build/macos/release/examples/<example>/...` |
| Visual Studio (`oncrpc.sln`) | `build\x64\Release\<example>_server.exe`, `<example>_client.exe` (`08_xdr_demo.exe`) |

The commands in each example's README use the Linux paths. Start a server
in one terminal and its client in another:

```sh
build/linux/release/examples/01_add/server       # Windows: build\x64\Release\01_add_server.exe
build/linux/release/examples/01_add/client       # Windows: build\x64\Release\01_add_client.exe
```

Servers take an optional port number (`server 6000`), and clients an
optional host and port (`client 127.0.0.1 6000`); `09_local` takes a socket
path instead (`server /tmp/my.sock`, `client /tmp/my.sock`). `make -f Makefile-<system>
check` (or `tests\run_tests.ps1` on Windows) runs every example;
`06_portmap` is skipped when no port mapper is running.

## What every example has in common

- **The `.x` file** describes the interface. `oncrpcgen name.x` generates
  `name.h` (types and prototypes), `name_xdr.c` (encoding and decoding),
  `name_clnt.c` (client functions, one per procedure) and `name_svc.c`
  (server dispatch and reply functions).
- **The server** implements one `oncRPC<Proc>V<version>Svc()` function per
  procedure and answers with `oncRPC<Proc>V<version>Reply()`; procedure
  `ADD` in version 1 gives `oncRPCAddV1Svc()` and `oncRPCAddV1Reply()`.
- **The client** calls `oncRPC<Proc>V<version>(client, arguments..., &result,
  callback, data)`, e.g. `oncRPCAddV1(...)`. The call returns at once; the callback runs when the
  answer arrives.
- **Your allocator.** Both sides pass a small allocator
  (`app_realloc`/`app_free`) to the library, which never calls malloc itself.
- **uv_run().** Both sides run inside `uv_run()`, so they can share their
  libuv loop with the rest of an application.
