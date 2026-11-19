# 08_xdr: XDR on its own

| File          | What it is                                                 |
|---------------|------------------------------------------------------------|
| `readings.x`  | an enum, a struct and an array type; no `program`          |
| `main.c`      | encodes a list of readings to bytes, decodes them back, and shows damaged input being rejected |

No networking and no libuv. Since `readings.x` has no program, the
generated header includes only `xdr.h`, and only `readings_xdr.c` is needed:

```sh
oncrpcgen -h -c readings.x
cc -std=c99 -o xdr_demo main.c readings_xdr.c -loncrpc     # no -luv
```

```sh
build/linux/release/examples/08_xdr/xdr_demo
# original (3 readings)
#   boiler       t=1763200000000 value=71.50   normal
#   ...
# encoded to 96 bytes; the first reading:
#   00 00 00 03 00 00 00 06 62 6f 69 6c 65 72 00 00
#   00 00 01 9a 86 e8 e0 00 40 51 e0 00 00 00 00 00
#   00 00 00 00
# decoded (3 readings)
#   ...
# decoding all but the last 5 bytes: rejected
```

Things to notice:

- **One routine, three jobs.** `oncRPCXdrReadingLog()` encodes, decodes or
  frees, depending on the stream you pass it.
- **Two kinds of encode buffer.** A dynamic encode stream grows through
  your allocator; `oncRPCXdrInitEncode()` uses a fixed buffer of yours
  instead.
- **Decoding allocates.** Strings and arrays are allocated while decoding.
  `oncRPCXdrFreeReadingLog()` releases them, including after a failed decode.
- **Platform-independent bytes.** The output is big-endian and padded to
  4-byte multiples, the same on every platform. These bytes are exactly what
  an RPC call carries.
