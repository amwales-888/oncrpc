# 11_auth: a home-made authentication flavor, and callers who stop waiting

| File       | What it is                                                         |
|------------|--------------------------------------------------------------------|
| `token.x`  | `WHOAMI`, `SLOW_ECHO` (answered 300 ms later) and `COUNT_ABANDONED` |
| `server.c` | checks a user name and password on every call; stops work nobody waits for |
| `client.c` | five steps, one after another                                      |

```sh
build/linux/release/examples/11_auth/server &
build/linux/release/examples/11_auth/client
# client: 1. WHOAMI with AUTH_NONE           authentication error           (as expected)
# client:    reason: too weak
# client: 2. WHOAMI as alice                 success                        (as expected)
# client:    server says: alice; its reply verifier checked out
# client: 3. WHOAMI as bob, wrong password   success                        (as expected)
# client:    server says: bob, after 1 refresh
# client: 4. SLOW_ECHO                       success                        (as expected)
# client:    answer: hello
# client: 5. SLOW_ECHO, then disconnect      cancelled                      (as expected)
# client: 5. COUNT_ABANDONED                 success                        (as expected)
# client:    the server stopped 1 request(s) nobody waited for
```

The server prints its side:

```
server: refused a caller with flavor 0
server: WHOAMI from alice
server: bad user name or password
server: WHOAMI from bob
server: SLOW_ECHO answers "hello"
server: SLOW_ECHO "never mind": caller went away, stopped
```

The "token" flavor sends `user:password` as the credential and proves both
sides know the password with checksums in the verifiers. The checksum is
not cryptographic: the example shows how a flavor plugs in, not how to
protect a password. For real use, also run the calls over TLS (`10_tls`).

Things to notice:

- **The client side of a flavor** is an `oncRPCClientAuth`, a struct of
  functions given to `oncRPCClientSetAuth()`. `credential` and `verifier`
  write each call's credential and verifier; `verifier` gets the encoded
  call header, so it can sign it. `validate` checks the verifier of each
  reply: a reply that fails the check fails the call with
  `ONCRPC_ERR_AUTHERROR`. `refresh` runs when the server refuses the
  credential and can supply a new one; the library then sends the call again
  by itself, which is what happens to bob in step 3.
- **The server side** is an authenticator, given to `oncRPCServerSetAuth()`
  for one flavor. It runs before any procedure, reads the credential and
  verifier with `oncRPCSvcCred()` and `oncRPCSvcVerf()`, sets the reply's
  verifier with `oncRPCSvcSetReplyVerf()`, and lets the call through or
  refuses it. The procedures never see a caller it refused.
- **Passing the identity on.** The authenticator stores the user it found
  in `req->authCtx`, where `WHOAMI` picks it up.
- **Turning away the standard flavors.** Authenticators for `AUTH_NONE`
  and `AUTH_SYS` that always refuse make the token the only way in (step 1).
- **Callers who stop waiting.** `SLOW_ECHO` answers later, from a timer.
  `oncRPCSvcSetAbandonCb()` asks the library to say when the caller's
  connection closes before then. The callback stops the timer and drops the
  request. Without it the server would do the work and send the answer
  into the void. A server that is closing also calls it, for every request
  still open, so it can finish them and close.
