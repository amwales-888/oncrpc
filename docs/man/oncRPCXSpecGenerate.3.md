# oncRPCXSpecGenerate(3)

## NAME

oncRPCXSpecGenerate - generate C code from a specification

## SYNOPSIS

```c
#include <xspec.h>

typedef enum oncRPCXGenKind {
    ONCRPC_GEN_HEADER,
    ONCRPC_GEN_XDR,
    ONCRPC_GEN_CLIENT,
    ONCRPC_GEN_SERVER,
    ONCRPC_GEN_SERVER_SKELETON,
    ONCRPC_GEN_CLIENT_SKELETON
} oncRPCXGenKind;

int oncRPCXSpecGenerate(const oncRPCXSpec *spec, oncRPCXGenKind kind,
                        const char *basename, char **out, size_t *outlen,
                        char *err, size_t errlen);
```

## DESCRIPTION

`oncRPCXSpecGenerate()` produces one of the files
[oncrpcgen(1)](oncrpcgen.1.md) writes, as text in memory:

| `kind` | File | Contents |
|---|---|---|
| `ONCRPC_GEN_HEADER` | `<base>.h` | types, constants and prototypes |
| `ONCRPC_GEN_XDR` | `<base>_xdr.c` | XDR routines for every type |
| `ONCRPC_GEN_CLIENT` | `<base>_clnt.c` | client stubs |
| `ONCRPC_GEN_SERVER` | `<base>_svc.c` | dispatch routines and registration |
| `ONCRPC_GEN_SERVER_SKELETON` | `<base>_server_skel.c` | a template for the server's procedures |
| `ONCRPC_GEN_CLIENT_SKELETON` | `<base>_client_skel.c` | an example client program |

`basename` names the header the generated `.c` files include
(`"<basename>.h"`) and is used in include guards.

On success, `*out` holds the NUL-terminated text and `*outlen` its length
without the NUL (if `outlen` is not `NULL`). The text is allocated from
the specification's allocator; release it with
[oncRPCFree(3)](oncRPCFree.3.md).

Before generating, the function works out every C name the code will
define and checks that none is used twice or clashes with the library's
own names - for instance `get_name` and `GET_NAME`, which would both become
`...GetName...` - so that the clash is reported with the `.x` names rather
than by the C compiler.

## RETURN VALUE

0 on success. -1 on error, with a message in `err` (if not `NULL`, cut to
`errlen` bytes): a name collision, a type that cannot be generated, an
argument that is `NULL`, or the allocator failing.

## EXAMPLE

Write the header for a parsed specification to standard output:

```c
#include <stdio.h>
#include <xspec.h>

int print_header(const oncRPCXSpec *spec)
{
    char *text, err[256];
    size_t len;

    if (oncRPCXSpecGenerate(spec, ONCRPC_GEN_HEADER, "service", &text, &len, err,
                            sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        return -1;
    }
    fwrite(text, 1, len, stdout);
    oncRPCFree(spec->alloc, text);
    return 0;
}
```

## SEE ALSO

[oncrpcgen(1)](oncrpcgen.1.md),
[oncRPCXSpecParse(3)](oncRPCXSpecParse.3.md)
