# 03_shapes: structs, unions, and several calls at once

| File       | What it is                                                    |
|------------|---------------------------------------------------------------|
| `shapes.x` | an enum, two structs and two discriminated unions             |
| `server.c` | computes areas; reports an error for impossible triangles     |
| `client.c` | starts four AREA calls together and collects the answers      |

```sh
build/linux/release/examples/03_shapes/server &
build/linux/release/examples/03_shapes/client
# client: circle, radius 1               area 3.1416
# client: rectangle 3 x 4                area 12.0000
# client: triangle 3, 4, 5               area 6.0000
# client: triangle 1, 1, 5               error: these sides do not make a triangle
```

Things to notice:

- **Unions in C.** A union becomes a struct holding the discriminant and a
  C union named `<union>_u`, for example `s->shape_u.radius` or
  `result.area_result_u.error`. Set the discriminant and the matching arm.
- **Concurrent calls.** Several calls can be outstanding on one client.
  Each needs its own result storage (`questions[i].result`).
- **Telling calls apart.** The `data` pointer given to `oncRPCAreaV1()` is handed
  back to the callback, which uses it to find the question.
- **Counting completions.** The client closes when `outstanding` reaches 0.
- **Freeing results.** `oncRPCXdrFreeAreaResult()` releases whatever decoding
  allocated (here the error string). Call it once you are finished with a
  successful result.
