# oncRPCXSpecParse(3)

## NAME

oncRPCXSpecParse - parse an XDR/RPC language specification

## SYNOPSIS

```c
#include <xspec.h>

oncRPCXSpec *oncRPCXSpecParse(const oncRPCAllocator *alloc, const char *text,
                              const char *filename, char *err, size_t errlen);
```

## DESCRIPTION

`oncRPCXSpecParse()` parses `text`, a NUL-terminated specification in the
XDR language (RFC 4506, section 6) and the RPC program language (RFC 5531,
section 12) - the contents of a `.x` file - and returns it as a tree of
definitions. `filename` is used only in error messages.

This is the parser behind [oncrpcgen(1)](oncrpcgen.1.md), offered as a
library function for tools that want to inspect interfaces: documentation
generators, linters, code generators for other languages, or a program that
generates code at build time without running `oncrpcgen`.

The language accepted, beyond the RFCs, is described in oncrpcgen(1):
`%` pass-through lines, ignored `#` lines, C and C++ comments, the type
names `char`, `short`, `long` and bare `unsigned`, enum items without
values, `string` as an argument or result type, inline struct, union and
enum definitions, and `struct name` style references. `quadruple` is
rejected.

The result is an `oncRPCXSpec`, whose `defs` field lists every top-level
definition in source order:

```c
typedef struct oncRPCXSpec {
    oncRPCXDef            *defs;
    const char            *filename;
    const oncRPCAllocator *alloc;
    void                  *priv;
} oncRPCXSpec;
```

Each `oncRPCXDef` has a `kind` (`ONCRPC_XDEF_CONST`, `_TYPEDEF`, `_ENUM`,
`_STRUCT`, `_UNION`, `_PROGRAM` or `_PASSTHROUGH`), a `name`, the source
`line`, the fields that apply to its kind, and `next`. `xspec.h` documents
every structure. Inline type definitions are given names of the form
`parent_field` and placed before their first use.

All memory, the returned structure included, comes from `alloc`; release
it with [oncRPCXSpecFree(3)](oncRPCXSpecFree.3.md). Parsing never blocks
and reads no files.

## RETURN VALUE

The specification, or `NULL` on error. Then, if `err` is not `NULL`, a
message of the form `file:line: error: message` is written to it, cut to
`errlen` bytes.

## EXAMPLE

List the programs and procedures of a `.x` file held in memory:

```c
#include <stdio.h>
#include <xspec.h>

void list_procedures(const oncRPCAllocator *a, const char *text)
{
    char err[256];
    oncRPCXSpec *spec = oncRPCXSpecParse(a, text, "service.x", err, sizeof(err));
    const oncRPCXDef *d;
    size_t v, p;

    if (spec == NULL) {
        fprintf(stderr, "%s\n", err);
        return;
    }
    for (d = spec->defs; d != NULL; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        printf("program %s = %s\n", d->name, d->value);
        for (v = 0; v < d->nversions; v++)
            for (p = 0; p < d->versions[v].nprocs; p++)
                printf("  version %s: %s = %s\n", d->versions[v].number,
                       d->versions[v].procs[p].name, d->versions[v].procs[p].number);
    }
    oncRPCXSpecFree(spec);
}
```

## SEE ALSO

[oncRPCXSpecFree(3)](oncRPCXSpecFree.3.md),
[oncRPCXSpecFind(3)](oncRPCXSpecFind.3.md),
[oncRPCXSpecEval(3)](oncRPCXSpecEval.3.md),
[oncRPCXSpecGenerate(3)](oncRPCXSpecGenerate.3.md),
[oncrpcgen(1)](oncrpcgen.1.md)
