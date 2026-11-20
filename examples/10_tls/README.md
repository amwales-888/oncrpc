# 10_tls: calls over TLS

| File           | What it is                                                      |
|----------------|-----------------------------------------------------------------|
| `hello.x`      | the interface: `string HELLO(string)`                           |
| `server.c`     | requires TLS, asks clients for a certificate, says who called   |
| `client.c`     | upgrades its connection to TLS and presents a certificate       |
| `pem.h`        | reads a PEM file (the library takes PEM text, not file names)   |
| `certs/`       | a test authority, and a server and client certificate it signed |

```sh
build/linux/release/examples/10_tls/server &
build/linux/release/examples/10_tls/client
# server: listening on 127.0.0.1:5010, TLS required
# server: HELLO from CN=test client,O=liboncrpc tests
# client: Hello, example client. Your certificate says you are CN=test client,O=liboncrpc tests.
kill %1
```

Run them from the repository root, or give the certificate directory:
`server [port] [certs]`, `client [host] [port] [certs]`.

Things to notice:

- **Nothing changes in the interface.** `hello.x`, the generated code, the
  procedure and the call are as in any other example. TLS is set up on the
  client and the server, with an `oncRPCTlsConfig` for each side.
- **How the connection is upgraded.** The client first sends a NULL call
  with the credential AUTH_TLS; the server answers "STARTTLS" and the TLS
  1.3 handshake follows on the same TCP connection (RFC 9289). Then the
  HELLO call goes through, encrypted.
- **What each side checks.** The client accepts the server only if
  `certs/ca.pem` signed its certificate and the certificate names the host
  it connected to (127.0.0.1 here; the certificate also names `localhost`).
  Try `client localhost` (fine) or replace `ca.pem` with another authority
  (the call fails with "TLS failed" and the reason).
- **Who is calling.** The server asks for a client certificate
  (`ONCRPC_TLS_VERIFY_PEER`) and, when one checks out against `ca.pem`,
  `oncRPCSvcTlsPeer()` gives its subject. `ONCRPC_TLS_VERIFY_REQUIRE` would
  turn away clients without one.
- **Requiring TLS.** `oncRPCServerSetTls(server, tls, 1)` refuses calls
  that do not come over TLS, with AUTH_TOOWEAK: point `01_add`'s client at
  this port to see it.
- **The certificates are for testing.** Their private keys are published
  with the source. `sh tests/make_test_certs.sh` makes new ones; for real
  use, get certificates from your own authority.
