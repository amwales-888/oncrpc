# 02_strings: strings, and calls in sequence

| File       | What it is                                                    |
|------------|---------------------------------------------------------------|
| `greet.x`  | `text HELLO(text)` and `text SHOUT(text)`, where `text` is a bounded string |
| `server.c` | builds the replies in local buffers                            |
| `client.c` | calls HELLO, then sends its answer to SHOUT                    |

```sh
build/linux/release/examples/02_strings/server &
build/linux/release/examples/02_strings/client
# client: HELLO -> "Hello, liboncrpc!"
# client: SHOUT -> "HELLO, LIBONCRPC!"
```

Things to notice:

- **Sequencing.** Calls run one after another by starting the next call in
  the previous call's callback (`on_hello_done` starts SHOUT).
- **Results are allocated.** A decoded string comes from your allocator.
  Release it with `oncRPCFree(&app_alloc, s)`.
- **Arguments are copied.** They are encoded before the stub returns, so
  `greeting` can be freed right after `oncRPCShoutV1()`.
- **Replies are copied.** The server's result can live in a stack buffer,
  because `*_reply()` encodes it immediately.
- **Bounds are enforced.** `string<MAX_TEXT>` is checked when encoding and
  decoding, on both sides.
