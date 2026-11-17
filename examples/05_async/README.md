# 05_async: replying later

| File       | What it is                                                    |
|------------|---------------------------------------------------------------|
| `delay.x`  | `unsigned int DELAY(unsigned int)`: answer after that many ms |
| `server.c` | starts a libuv timer per request and replies when it fires    |
| `client.c` | sends 300, 100 and 200 ms requests together                   |

```sh
build/linux/release/examples/05_async/server &
build/linux/release/examples/05_async/client
# client: sending DELAY(300)
# client: sending DELAY(100)
# client: sending DELAY(200)
# client: DELAY(100) answered after 101 ms
# client: DELAY(200) answered after 201 ms
# client: DELAY(300) answered after 301 ms
```

Things to notice:

- **Answering later.** A procedure does not have to answer before it
  returns. Keep the `oncRPCSvcReq *` and call the `*_reply()` function
  whenever the result is ready: from a timer, a file read, a database
  callback, or a call to another RPC server. The request and its arguments
  stay valid until then.
- **Nothing blocks.** While requests wait, the server serves other
  requests and connections. Both programs run entirely on their libuv loop.
- **Out-of-order replies.** Replies on one connection come back in
  whatever order they are sent. The client matches them to calls.
- **Every request needs an answer.** Finish each one with a reply, an
  `oncRPCSvcErr*()` function or `oncRPCSvcDrop()`. Closing a server waits
  for requests that are still open.
