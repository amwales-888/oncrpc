# 04_lists: arrays and linked lists

| File       | What it is                                                    |
|------------|---------------------------------------------------------------|
| `lists.x`  | `double samples<1000>` (an array) and a `word` linked list    |
| `server.c` | summarises the numbers; sorts the words                       |
| `client.c` | sends both and prints the results                             |

```sh
build/linux/release/examples/04_lists/server &
build/linux/release/examples/04_lists/client
# client: 6 numbers, min -2, max 10, mean 3.875
# client: sorted: alpha bravo charlie delta
```

Things to notice:

- **Arrays in C.** `double samples<1000>` becomes
  `struct { uint32_t samples_len; double *samples_val; }`. To send one, point
  `samples_val` at your data and set `samples_len`.
- **Linked lists.** `word *next` is optional data: NULL ends the list. A
  list to send can be built from local variables.
- **Freeing a list.** A received list is allocated node by node.
  `xdr_free_word_list()` frees all of it.
- **Bounds.** The server refuses more than 1000 numbers and words longer
  than 32 characters, because the interface says so.
