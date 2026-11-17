# 01_add: add two numbers

The smallest complete client and server.

| File       | What it is                                                    |
|------------|---------------------------------------------------------------|
| `add.x`    | the interface: one procedure, `int ADD(int, int)`             |
| `server.c` | implements `add_1_svc()` and serves on 127.0.0.1:5001          |
| `client.c` | calls `add_1()` once and prints the answer                     |

oncrpcgen turns `add.x` into `add.h`, `add_xdr.c`, `add_clnt.c` (used by the
client) and `add_svc.c` (used by the server). The build does this for you:
the makefiles generate into `build/<system>/release/examples/01_add/`, and
Visual Studio into each project's intermediate directory.

```sh
build/linux/release/examples/01_add/server &
build/linux/release/examples/01_add/client
# server: 2 + 3 = 5
# client: 2 + 3 = 5
```

Things to notice:

- `add_1()` returns at once. The answer arrives later in `on_add_done()`,
  which libuv calls from `uv_run()`.
- The result variable `sum` is static because it must still exist when the
  reply is written into it.
- The client closes itself when it is done. That is what lets `uv_run()`
  return.
