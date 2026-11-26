# oncrpcgen(1)

## NAME

oncrpcgen - generate C code for liboncrpc from an XDR/RPC specification

## SYNOPSIS

```
oncrpcgen [-a] [-h] [-c] [-l] [-s] [-S] [-C] [-o DIR | -O FILE] [-b NAME] [-f] file.x
oncrpcgen --help | --version
```

## DESCRIPTION

`oncrpcgen` reads a specification of data types and RPC programs, written
in the XDR language (RFC 4506) and the RPC program language (RFC 5531),
and writes C code that uses liboncrpc:

| Option | File | Contents |
|---|---|---|
| `-h` | `<base>.h` | types, constants and prototypes |
| `-c` | `<base>_xdr.c` | an XDR routine for every type |
| `-l` | `<base>_clnt.c` | client stubs: one function per procedure |
| `-s` | `<base>_svc.c` | dispatch routines and registration functions |
| `-S` | `<base>_server_skel.c` | a template for the server's procedures, to fill in |
| `-C` | `<base>_client_skel.c` | an example client program |
| `-a` | the first four | (the default when no output option is given) |

`<base>` is the input file's name without directory and `.x`.

## OPTIONS

`-o DIR`
: Write the files into `DIR` instead of the current directory.

`-O FILE`
: Write a single output (exactly one of `-h -c -l -s -S -C`) to `FILE`;
  `-` means standard output.

`-b NAME`
: Use `NAME` as `<base>`: in file names and in the `#include` of the
  generated header.

`-f`
: Overwrite existing skeleton files. Without it, `-S` and `-C` refuse to
  replace a file, since it may hold the application's own code.

`--help`, `--version`
: Show usage or the version, and exit.

## THE LANGUAGE

The input follows RFC 4506 section 6 and RFC 5531 section 12:

```
const MAX_NAME = 32;

enum color { RED = 0, GREEN = 1, BLUE = 2 };

struct person {
    string  name<MAX_NAME>;     /* a string of at most 32 bytes */
    int     age;
    color   favourite;
    person *next;               /* optional data: a linked list */
};

union lookup_result switch (int status) {
case 0:  person who;
default: void;
};

program PEOPLE_PROG {
    version PEOPLE_V1 {
        lookup_result FIND(string) = 1;
        int           COUNT(void)  = 2;
    } = 1;
} = 0x20000300;
```

Program numbers from 0x20000000 to 0x3fffffff are free for private use.

These extensions are accepted:

- lines starting with `%` are copied unchanged into the header;
- lines starting with `#` are ignored;
- C (`/* */`) and C++ (`//`) comments;
- the type names `char`, `short`, `long` and bare `unsigned`;
- enum items without `= value` continue from the previous item;
- `string` as a procedure's argument or result type;
- struct, union and enum types defined inline inside another type, which
  are given names of the form `parent_field`;
- `struct name`, `union name` and `enum name` as type references.

`quadruple` is rejected, as C has no portable type for it.

## GENERATED CODE

Every C name begins with `oncRPC` followed by the `.x` name in CamelCase:
`GET_FIRST` becomes `GetFirst`, `name_t` becomes `NameT`. Constants and
types keep their `.x` names.

| `.x` | Generated |
|---|---|
| `const MAX = 8;` | `#define MAX 8` |
| `enum color {...}` | `typedef enum color {...} color;` |
| `struct person {...}` | `struct person`, `typedef struct person person;` |
| `union result switch (...)` | `struct result` with the discriminant and a union `result_u` |
| `string s<N>` | `char *s` |
| `opaque o<N>` | `struct { uint32_t o_len; uint8_t *o_val; } o` |
| `T a<N>` | `struct { uint32_t a_len; T *a_val; } a` |
| `T *p` | `T *p` (optional) |
| `int` / `unsigned` / `hyper` / `bool` | `int32_t` / `uint32_t` / `int64_t` / `oncRPCBool` |

For every type `T`:

- `int oncRPCXdr<T>(oncRPCXdr *xdrs, T *objp)` encodes, decodes or frees it;
- `void oncRPCXdrFree<T>(const oncRPCAllocator *alloc, T *objp)` releases
  what decoding it allocated.

For procedure `P` of version `n` (a program number and version number are
also defined as macros):

| Function | Side | Purpose |
|---|---|---|
| `oncRPC<P>V<n>(clnt, args..., res, cb, data)` | client | start a call, like [oncRPCClientCall(3)](oncRPCClientCall.3.md); arguments are passed by pointer (strings as `const char *`), results are written to `*res` |
| `void oncRPC<P>V<n>Svc(args..., req)` | server | **written by you**: called for each request |
| `int oncRPC<P>V<n>Reply(req, &res)` | server | send the result, like [oncRPCSvcReply(3)](oncRPCSvcReply.3.md) |
| `oncRPC<Program>V<n>Register(srv, ctx)` | server | serve the version, like [oncRPCServerRegister(3)](oncRPCServerRegister.3.md) |

A procedure with several arguments also gets a structure
`oncRPC<P>V<n>Args` holding them. The generated dispatch routine decodes
the arguments, calls the `Svc` function, answers procedure 0 (the NULL
procedure, a "ping") even when the `.x` file does not declare it, and
answers unknown procedures with PROC_UNAVAIL.

The `Svc` function must finish the request exactly once, immediately or
later: with the `Reply` function, an `oncRPCSvcErr*` function, or
`oncRPCSvcDrop()`. Its arguments stay valid until then.

## EXIT STATUS

0 on success; 1 if the input cannot be read or has an error, the code
would have a name clash, or a file could not be written; 2 for wrong
options. Errors in the input are reported as `file:line: error: message`.

## EXAMPLES

Generate everything for `people.x` into `gen/`, plus a server template:

```sh
oncrpcgen -o gen people.x
oncrpcgen -S -o gen people.x
```

Build a client and a server with the generated files:

```sh
cc -std=c99 -D_POSIX_C_SOURCE=200809L -I/usr/local/include/oncrpc -Igen -c \
   gen/people_xdr.c gen/people_clnt.c gen/people_svc.c client.c server.c
cc -o client client.o people_xdr.o people_clnt.o -loncrpc -luv
cc -o server server.o people_xdr.o people_svc.o -loncrpc -luv
```

Look at the header without writing a file:

```sh
oncrpcgen -h -O - people.x | less
```

## SEE ALSO

[liboncrpc(3)](liboncrpc.3.md),
[oncRPCXSpecParse(3)](oncRPCXSpecParse.3.md),
[oncRPCXSpecGenerate(3)](oncRPCXSpecGenerate.3.md),
[oncRPCClientCall(3)](oncRPCClientCall.3.md),
[oncRPCServerRegister(3)](oncRPCServerRegister.3.md)
