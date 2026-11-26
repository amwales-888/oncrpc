# liboncrpc: still to do

The library is complete for ordinary RPC clients and servers (version
4.3.0). This file lists what is left, in the suggested order. Nothing here
blocks normal use; the later sections are worth doing only when someone
needs them.

## 1. Verification on real platforms

- [ ] **Windows.** Build and run the 4.2.0 and 4.3.0 work in Visual Studio:
  the `liboncrpc_shared` project (`oncrpc.dll`), `test_rpc_shared`, the
  `11_auth` example, and the new `test_rpc` sections (authentication,
  deferred replies, cancelling calls). So far these have only been
  cross-compiled with MinGW, not run. `tests\run_tests.ps1` covers all of it.
- [ ] **macOS.** One build on a Mac to confirm the dylib link options
  (`-dynamiclib -install_name @rpath/...`), the `@loader_path/..` search
  path used by `test_rpc_shared`, and `install_name_tool -id` in
  `make install`. The rest of `Makefile-macOS` has been exercised on Linux.

## 2. Interoperability testing

The library has only ever talked to itself, so a detail of the RFCs misread
the same way on both sides would go unnoticed. Test against other
implementations (used only by the tests, never by the library):

- [ ] A client and a server built on libtirpc, calling our server and
  answered by our client: every XDR type, AUTH_SYS, errors (unknown
  program, version mismatch, garbage arguments), large multi-fragment
  records over TCP, UDP.
- [ ] The system `rpcbind` and `rpcinfo`: registering, unregistering and
  looking up through versions 2, 3 and 4 (`test_rpcb` currently skips these
  parts when no rpcbind is running).
- [ ] Wireshark's dissector (`tshark`) reading captured traffic, as an
  independent check of the bytes on the wire, including RPC-with-TLS.
- [ ] A real service: the NULL procedure of an NFS server's MOUNT program,
  and of NFS itself.

## 3. Fuzzing

- [ ] libFuzzer (with ASan and UBSan) harnesses for everything that reads
  untrusted input:
  - the XDR decoder (through generated code for a `.x` with every type),
  - record marking (fragment headers, lengths, limits),
  - the server's call parser (header, credentials, verifiers),
  - the client's reply parser,
  - universal-address parsing (`oncRPCUaddrToSockaddr`).
- [ ] The `.x` parser as well (less exposed: `.x` files come from the
  developer).
- [ ] Keep the crash corpus as regression tests in `tests/`.
- [ ] **Enum checking on decode.** Generated code does not check that a
  received enum value is one of the declared items (USAGE.md, section 12).
  Reject other values as `ONCRPC_ERR_CANTDECODE`. Do this alongside the
  fuzzing.

## 4. Small gaps

- [ ] **rpcbind DUMP.** List every registration, as `rpcinfo -p` does
  (portmapper version 2 `PMAPPROC_DUMP`, rpcbind 3/4 `RPCBPROC_DUMP`).
- [ ] **Replies around the switch to TLS.** A reply to a request that
  arrived before a connection switched to TLS, and is sent after the switch,
  is dropped. Hold it until the handshake has finished, or send it in the
  clear before the switch, whichever RFC 9289 implies. Only a client sending
  calls alongside its STARTTLS request would notice.
- [ ] **TLS close_notify.** Send it before closing a TLS connection; some
  strict TLS peers log an error without it.

## 5. Protocol features, only if needed

- [ ] **The other rpcbind procedures:** CALLIT / INDIRECT (call a service
  through rpcbind), GETTIME, GETADDRLIST, GETSTAT, GETVERSADDR.
- [ ] **UDP broadcast calls:** one call to a broadcast address, collecting
  every reply until a timeout.
- [ ] **Batching:** sending many calls without waiting for each reply (calls
  with no reply expected, flushed by an ordinary call).
- [ ] **Callbacks over the client's own connection:** the server calling the
  client back over the connection the client opened, as NFSv4.1 does.
- [ ] **RPCSEC_GSS (Kerberos, RFC 2203).** A large job: needs GSS-API and
  wrapping of arguments and results, which the pluggable authentication
  (`oncRPCClientSetAuth`, `oncRPCServerSetAuth`) does not cover. TLS now
  covers most of the need for privacy.
- [ ] **TLS extras:** DTLS for UDP, TLS on local sockets, session
  resumption.
- [ ] **Old flavors:** AUTH_SHORT and AUTH_DH.
- [ ] **`quadruple`** in `.x` files (currently rejected: C has no portable
  128-bit floating type).
- **Threads:** not planned. The library follows libuv: one loop, one thread.
  Supporting several threads would be a redesign, not an addition.

## 6. Packaging and documentation

- [ ] **pkg-config file** (`oncrpc.pc`), installed by `make install`, so other
  projects can use `pkg-config --cflags --libs oncrpc`.
- [ ] **vcpkg port**, so other Windows projects can depend on the library.
- [ ] **Continuous integration** building and testing on Linux, FreeBSD,
  macOS and Windows on every change (gcc, clang, sanitizers, valgrind,
  `check-exports`, `check-alloc`, the soak test).
- [x] **Per-function reference documentation** for all 114 public
  functions: manual pages in `docs/man/`, with a getting-started tutorial
  in `docs/TUTORIAL.md`.
- [ ] **CHANGES.md** recording what each version added (4.0 to 4.3 so far).
- [ ] **Versioning rule**, written down in CHANGES.md or BUILDING.md: the
  major version (4), which the shared library carries in its name, changes
  only when existing programs must be rebuilt; additions bump the minor
  version.
