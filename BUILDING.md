# Building liboncrpc

The library, the `oncrpcgen` code generator, the examples and the tests
build on Linux, FreeBSD and Windows from the same sources, which are all in
`src/`, and there is an **experimental, untested** macOS port (see below).
Build output goes in `build/` (Linux in `build/linux`, FreeBSD in
`build/freebsd`, macOS in `build/macos`, Visual Studio in `build/x64` and
`build/obj`, so they can sit side by side, for example on a shared disk).

There is one makefile per system: `Makefile-Linux` (GNU make),
`Makefile-FreeBSD` (FreeBSD's own make) and `Makefile-macOS` (the GNU make
3.81 that comes with macOS), so give it with `-f`. Visual Studio uses
`oncrpc.sln`.

libuv does almost all of the platform work. The only platform-specific code
in liboncrpc reads the process's identity for AUTH_SYS credentials
(`oncRPCClientSetAuthSysDefault()` in `src/client.c`, `#ifndef _WIN32`) and
a local socket peer's (`src/server.c`), and `src/internal.h` asks glibc for
POSIX declarations on Linux (below).

Dependencies: **libuv** (1.27 or later) and **OpenSSL 3** (for TLS,
`src/tls.c`). `oncrpcgen` and the XDR-only parts of the library need
neither.

## What gets built

| Output (Linux shown; FreeBSD and macOS alike) | What it is |
|---|---|
| `build/linux/release/liboncrpc.a` | the static library |
| `build/linux/release/liboncrpc.so.4.1.0`, with the links `liboncrpc.so.4` and `liboncrpc.so` | the shared library (FreeBSD: `liboncrpc.so.4` and `liboncrpc.so`; macOS: `liboncrpc.4.dylib` and `liboncrpc.dylib`) |
| `build/linux/release/oncrpcgen` (and a `./oncrpcgen` link) | the `.x` compiler |
| `build/linux/release/examples/<example>/server`, `client` | the examples (`examples/README.md`); `08_xdr` builds `xdr_demo` |
| `build/linux/release/examples/<example>/*.h`, `*.c` | the code oncrpcgen generated for each example |
| `build/linux/release/tests/` | the tests (built by `check`) |

Visual Studio puts every program in `build\x64\<Configuration>`, named after
its project (`oncrpcgen.exe`, `01_add_server.exe`, `01_add_client.exe`, ...,
`08_xdr_demo.exe`, `test_rpc.exe`, `test_tls.exe`, `test_soak.exe`), with the
static library `liboncrpc.lib` and the DLL `oncrpc.dll` (and its import
library `oncrpc.lib`); see below.

## Static and shared libraries

Every build makes both, from the same objects: the static library and the
shared one (`liboncrpc.so`, `liboncrpc.dylib` or `oncrpc.dll`). The shared
library exports exactly the functions declared `ONCRPC_API` in the headers
(the 105 functions of the public API) and keeps the library's internal
functions hidden; `check` verifies this (`tests/check_exports.sh`) and runs
`test_rpc` a second time linked with it (`test_rpc_shared`).

Its name carries the major version (`liboncrpc.so.4`, `liboncrpc.4.dylib`),
which changes when programs built against the library must be rebuilt;
the minor and patch versions do not. On Windows the DLL is `oncrpc.dll`;
programs that use it **define `ONCRPC_SHARED`** when compiling, so that the
headers declare the functions as imported. Programs that use a static
library define nothing.

## Linux (GNU make)

```sh
sudo apt install build-essential pkg-config libuv1-dev libssl-dev   # Debian/Ubuntu
make -f Makefile-Linux            # library, oncrpcgen and the examples in build/linux/release
make -f Makefile-Linux check      # run the tests and every example (see "Tests" below)
make -f Makefile-Linux debug      # AddressSanitizer + UBSan build in build/linux/debug
make -f Makefile-Linux debug check   # ... and run the tests under the sanitizers
make -f Makefile-Linux soak       # the soak test (see "Tests"); SEED=7 SCALE=10 for a longer run
make -f Makefile-Linux lib        # only liboncrpc.a
make -f Makefile-Linux install PREFIX=$HOME/.local
make -f Makefile-Linux clean      # removes build/linux only
```

Variables: `CC`, `CFLAGS`, `PREFIX`, `DESTDIR`, `V=1` (show the full
commands). `install` puts `oncrpcgen` in `$PREFIX/bin`, `liboncrpc.a` in
`$PREFIX/lib` and `oncrpc.h`, `xdr.h` and `xspec.h` in
`$PREFIX/include/oncrpc`.

Everything is compiled with `-std=c99 -Wall -Wextra -Wpedantic -Wshadow
-Wconversion`, with no warnings, including the code that oncrpcgen
generates. glibc hides the POSIX declarations that `<uv.h>` needs under
strict `-std=c99`, so Makefile-Linux defines `_POSIX_C_SOURCE=200809L`, and
the library's sources, the generated code and the examples define it
themselves on Linux (only on Linux) if it is not set.

Running an example:

```sh
cd build/linux/release/examples/01_add
./server &
./client
# client: 2 + 3 = 5
kill %1
```

## FreeBSD (the system's make)

FreeBSD 13 or later. libuv comes from packages; OpenSSL 3 comes with the
base system (FreeBSD 14 and later; on 13, `pkg install openssl` and see
`OPENSSLBASE` below). The compiler is the system's `cc` (clang).

```sh
pkg install libuv pkgconf                 # as root
make -f Makefile-FreeBSD                  # build/freebsd/release (and ./oncrpcgen link)
make -f Makefile-FreeBSD check            # tests and examples
make -f Makefile-FreeBSD soak             # the soak test; SEED=7 SCALE=10 for a longer run
make -f Makefile-FreeBSD debug            # AddressSanitizer + UBSan build in build/freebsd/debug
make -f Makefile-FreeBSD install          # as root: /usr/local/bin, /usr/local/lib, /usr/local/include/oncrpc
make -f Makefile-FreeBSD install PREFIX=$HOME/.local
make -f Makefile-FreeBSD clean            # removes build/freebsd only
```

Variables: `CC`, `CFLAGS`, `PREFIX`, `DESTDIR`, `V=1` (show the full
commands), `LOCALBASE` (where packages are, `/usr/local`) and
`OPENSSLBASE` to build against OpenSSL from packages instead of the base
system (`make -f Makefile-FreeBSD OPENSSLBASE=/usr/local`; the programs then
find it at run time through an rpath). Makefile-FreeBSD is written for
FreeBSD's make (bmake); if you prefer GNU make, `pkg install gmake` and
`gmake -f Makefile-Linux` also works, as that file only needs pkg-config to
find libuv and OpenSSL.

Unlike Makefile-Linux, Makefile-FreeBSD does not define `_POSIX_C_SOURCE`:
on FreeBSD that hides the BSD interfaces that the system's own network
headers rely on, and FreeBSD's default environment already has everything
libuv needs. The sources only define it on Linux.

The `06_portmap` example needs the port mapper: `sysrc rpcbind_enable=YES`
and `service rpcbind start`. `check` skips it when no port mapper answers.

Status: Makefile-FreeBSD has been run with bmake (on Linux, against Linux's
headers, with `LOCALBASE=/usr PKG_CONFIG=pkg-config`): it builds everything
and `check` passes, and incremental rebuilds work. It has not yet been run
on a FreeBSD machine.

## macOS (EXPERIMENTAL: not yet tested on a Mac)

> **Status: experimental and untested.** Makefile-macOS has been written
> and its rules exercised (with GNU make 4 on Linux), but it has never been
> run with Apple's make 3.81, the Xcode tools or Homebrew's libuv on a Mac.
> Expect to find problems the first time; please report the output so they
> can be fixed. Linux, FreeBSD and Windows are unaffected by it.

macOS 12 or later, on Apple silicon or Intel. You need the Xcode Command
Line Tools (for `cc`, which is Apple's clang, and `make`) and
[Homebrew](https://brew.sh) for libuv, OpenSSL 3 and pkg-config:

```sh
xcode-select --install                       # once, if not done already
brew install libuv openssl@3 pkg-config
make -f Makefile-macOS                       # build/macos/release (and ./oncrpcgen link)
make -f Makefile-macOS check                 # tests and examples
make -f Makefile-macOS soak                  # the soak test; SEED=7 SCALE=10 for a longer run
make -f Makefile-macOS debug                 # AddressSanitizer + UBSan build in build/macos/debug
make -f Makefile-macOS install PREFIX=$HOME/.local
make -f Makefile-macOS clean                 # removes build/macos only
```

Variables: `CC`, `CFLAGS`, `PREFIX`, `DESTDIR`, `V=1` (show the full
commands), `OPENSSL_PREFIX` and `ARCHFLAGS`. Homebrew installs OpenSSL 3 out
of the way ("keg-only"), so the makefile asks `brew --prefix openssl@3`
where it is; set `OPENSSL_PREFIX=...` to use another OpenSSL 3. Like
Makefile-FreeBSD, it does not define
`_POSIX_C_SOURCE` (on macOS that hides the BSD interfaces the system headers
rely on); it defines `_DARWIN_C_SOURCE` instead, as does `src/internal.h`.

**Universal binaries** (for Apple silicon and Intel together) need libuv
and OpenSSL built for both as well, which Homebrew does not do: build them
yourself with `-arch arm64 -arch x86_64`, point pkg-config at them, and then
`make -f Makefile-macOS ARCHFLAGS="-arch arm64 -arch x86_64"`. A plain build
runs on the kind of Mac it was built on.

macOS includes `rpcbind`, which `06_portmap` needs; `check` skips that
example when no port mapper answers.

## Windows (Visual Studio 2026)

The solution uses platform toolset **v145** (Visual Studio 2026) and gets
libuv and OpenSSL through **vcpkg manifest mode** (`vcpkg.json`). Visual
Studio 2026 includes vcpkg.

### One-time setup

1. In the Visual Studio Installer, make sure the **Desktop development with
   C++** workload is installed. Its **vcpkg package manager** component is
   included by default.
2. Open **Developer PowerShell for VS 2026** (from the Start menu or
   *Tools > Command Line*) and run:

   ```powershell
   vcpkg integrate install
   ```

   This lets MSBuild use vcpkg for every project. You only do it once per
   machine.

### Build

1. Open `oncrpc.sln`.
2. Choose **Release | x64** (or Debug) and **Build > Build Solution**.

The first build downloads and compiles libuv and OpenSSL into
`vcpkg_installed\`, which takes several minutes; later builds reuse them.
Output goes to `build\x64\Release\`, with the libuv and OpenSSL DLLs copied
next to the programs; the static library is `build\x64\Release\liboncrpc.lib`.

From the command line (Developer PowerShell):

```powershell
msbuild oncrpc.sln /p:Configuration=Release /p:Platform=x64 /m
```

The solution has these projects:

| Project | Builds | From |
|---|---|---|
| `liboncrpc` | `liboncrpc.lib` (static) | `src\*.c` |
| `liboncrpc_shared` | `oncrpc.dll` and its import library `oncrpc.lib` | `src\*.c`, with `ONCRPC_SHARED` and `ONCRPC_BUILDING` defined |
| `oncrpcgen` | `oncrpcgen.exe` | `src\oncrpcgen.c` + liboncrpc |
| `examples\01_add_server`, `01_add_client`, ... `07_errors_client`, `09_local_server`, `09_local_client` | `<project>.exe` | `examples\<example>\server.c` or `client.c` + generated code |
| `examples\08_xdr_demo` | `08_xdr_demo.exe` | `examples\08_xdr\main.c` + generated code |
| `tests\test_xdr`, `test_xspec`, `test_gen`, `test_rpc`, `test_rpcb`, `test_tls`, `test_soak` | `<project>.exe` | `tests\*.c` |
| `tests\test_rpc_shared` | `test_rpc_shared.exe` | `tests\test_rpc.c`, linked with `oncrpc.dll` |

**Generated code.** Each example and test project has its `.x` file as a
*Custom Build Tool* item: before compiling, it runs
`build\x64\<Configuration>\oncrpcgen.exe` on it, writing the C files into
the project's intermediate directory
(`build\obj\<project>\x64\<Configuration>\gen\`), and compiles the ones it
needs from there. Editing the `.x` file regenerates them on the next build.
The projects reference `oncrpcgen` so that it is built first, and
`liboncrpc`, which they link with.

### Running

Start a server in one window and its client in another:

```powershell
cd build\x64\Release
.\01_add_server.exe            # server: listening on 127.0.0.1:5001
.\01_add_client.exe            # (in a second window) client: 2 + 3 = 5
```

Servers take an optional port and clients an optional host and port, as on
the other systems (`examples\README.md`). `06_portmap` needs a port mapper,
which Windows does not have, so it only works against a server on another
system: `.\06_portmap_client.exe some-linux-host`.

To pass arguments when debugging in Visual Studio, use the project's
*Properties > Debugging > Command Arguments*; to debug a client and its
server together, set both as startup projects (*Solution > Properties >
Common Properties > Startup Project > Multiple startup projects*).

### Tests

After building the solution, from the repository root:

```powershell
.\tests\run_tests.ps1                     # Release build
.\tests\run_tests.ps1 -Configuration Debug
```

It runs the tests (`test_xdr`, `test_xspec`, `test_gen`, `test_rpc`,
`test_rpc_shared`, `test_rpcb`, `test_tls`; `-Soak` adds `test_soak`), then each example's
server and client on ports 23401 to 23410 of 127.0.0.1 (`-BasePort` moves
them), skipping `06_portmap`, and exits with 0 if everything passed. If PowerShell refuses
to run scripts, use `powershell -ExecutionPolicy Bypass -File
tests\run_tests.ps1`.

### Using liboncrpc in your own Visual Studio solution

1. Add `liboncrpc.vcxproj` (static) or `liboncrpc_shared.vcxproj` (DLL),
   and `oncrpcgen.vcxproj` if you generate code in the build, to your
   solution, and to your project add a reference to the library
   (*References > Add Reference*). With the DLL, also add `ONCRPC_SHARED` to
   *C/C++ > Preprocessor > Preprocessor Definitions*, and ship `oncrpc.dll`
   (with the libuv and OpenSSL DLLs) next to your program.
2. Add liboncrpc's `src` folder to *C/C++ > General > Additional Include
   Directories*, and add libuv and OpenSSL to your `vcpkg.json` (or use this
   one).
3. To generate code from your `.x` file as part of the build, copy the
   `CustomBuild` item from one of the example projects (for example
   `examples\01_add\01_add_server.vcxproj`), and add the generated `.c` files
   you need (`<name>_xdr.c`, and `<name>_clnt.c` or `<name>_svc.c`) as
   `ClCompile` items. Alternatively, run oncrpcgen once and add its output
   to your project like any other source.

### Status

Built with Visual Studio 2026 Community with no warnings, and every test
and example passes, TLS included (`tests\run_tests.ps1`, and `-Soak`).

## Tests

`check` (or `tests\run_tests.ps1` on Windows) runs:

| Test         | What it covers |
|--------------|----------------|
| `test_xdr`   | XDR primitives against hand-worked byte layouts, bounds, failures |
| `test_xspec` | the `.x` parser: every construct, error messages and line numbers |
| `test_gen`   | generated XDR code for every declaration form: encode, decode, re-encode, truncated input; with make, the generated client, server and both skeletons are also built with `-Werror` |
| `test_rpc`   | a client and a server in one loop: 200 concurrent calls, deferred replies, TCP and UDP, timeouts, retransmission, cancellation, refused connections, unknown hosts, reconnection, a server closing while requests are open, the same calls over a local socket (with the caller's uid and gid, a path already in use, the socket file removed on close), the UDP duplicate-request cache (replays, ignored resends, expiry, eviction, size limits), the server limits (connections, outstanding requests, pausing and resuming connections, idle timeout), and two out-of-memory sweeps that fail each allocation in turn and check that nothing leaks |
| `test_tls`   | RPC-with-TLS: the AUTH_TLS request and the STARTTLS answer byte by byte; calls over TLS (many at once, a 300 KB string there and back); the server's certificate checked against a trusted authority, its DNS name and its IP address, and checking switched off; mutual TLS with a client certificate required, missing or untrusted, and the subject the server sees; a server without TLS; a server requiring TLS (refusing plain TCP and UDP, allowing local sockets); a TLS connection closed when idle and remade; configuration errors. The certificates are in `tests/tls_certs.h` |
| `check-alloc`| (make only) `nm` check that the library never references the C allocator |
| `check-exports` | (make only) the shared library exports every function declared `ONCRPC_API` and nothing else |
| `test_rpc_shared` | `test_rpc` linked with the shared library instead of the static one |
| `test_rpcb`  | universal addresses (IPv4, IPv6, malformed input); with rpcbind running, entries set, looked up and removed by network id, a server's listeners registered and found by clients over TCP and UDP, and its local socket registered as `local` and found by its path; with nothing on port 111 and permission to listen there, a stand-in version 2 port mapper that registration and look-ups must fall back to. The parts that cannot run are skipped |
| examples     | every example's client against its server; `06_portmap` only if a port mapper answers; `09_local` over a socket in `$TMPDIR` (a named pipe on Windows); `10_tls` with the certificates in `examples/10_tls/certs` (`tests/run_examples.sh`) |

The soak test runs separately, with `make soak` or
`tests\run_tests.ps1 -Soak`, as it takes about ten seconds:

| Test         | What it covers |
|--------------|----------------|
| `test_soak`  | 60 clients (40 over UDP, 10 over TCP, 10 over a local socket) against one server with small limits (4 requests per connection, 32 in all, 40 ms idle timeout). UDP passes through a relay that loses 15% of datagrams, duplicates 5% and delays 20%; some calls are answered late. Every call carries an id and the server counts how often each runs. With a cache large enough to hold every request, no UDP call may run twice; with a tiny one, calls may run again but every answer must be right and the cache must stay within its limits. Also checks that every call either succeeds or fails only in the ways the conditions allow, that the server's counters agree with what the procedures saw, that the connection limit refuses the surplus clients, and that nothing leaks. |

`test_soak [seed [scale]]` (or `SEED=`/`SCALE=` with make, `-Seed`/`-Scale`
with PowerShell) chooses the random sequence and multiplies the number of
calls. The seed fixes the relay's and the clients' decisions, but which
datagram meets which decision also depends on timing, so a failing seed
makes a failure likely to recur rather than certain.

The examples use ports 23401 to 23410 on 127.0.0.1, below the range from
which systems assign ports automatically (so that no connection left over
from an earlier test can be holding one); set `ONCRPC_TEST_PORT` (make) or
`-BasePort` (PowerShell) to move them.

The test and example certificates (in `tests/tls_certs.h` and
`examples/10_tls/certs`) are made by `sh tests/make_test_certs.sh`, which
needs the `openssl` command. They are valid until 2126, and their private
keys are public: use them for nothing but the tests and examples.

## Using the library in your own makefiles

After `make -f Makefile-<system> install` (or with `-I` pointing at `src/`
and the library from `build/`):

```sh
oncrpcgen myproto.x               # myproto.h, myproto_xdr.c, myproto_clnt.c, myproto_svc.c
cc -std=c99 -I/usr/local/include/oncrpc $(pkg-config --cflags libuv openssl) -c \
   myclient.c myproto_xdr.c myproto_clnt.c
cc -o myclient myclient.o myproto_xdr.o myproto_clnt.o -L/usr/local/lib -loncrpc \
   $(pkg-config --libs libuv openssl)
```

`-loncrpc` links the shared library when there is one in the directory,
as there is after `install`; the program then needs it at run time
(installed in a system directory, followed by `ldconfig` on Linux; or found
through `-Wl,-rpath,/usr/local/lib`). To link the static library instead,
name it: `/usr/local/lib/liboncrpc.a` in place of `-loncrpc`.

On Linux with strict `-std=c99`, your own files that include `oncrpc.h` need
`-D_POSIX_C_SOURCE=200809L` (or `-std=gnu99`); files generated by oncrpcgen
take care of themselves.
