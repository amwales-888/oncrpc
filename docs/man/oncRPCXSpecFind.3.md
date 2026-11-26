# oncRPCXSpecFind(3)

## NAME

oncRPCXSpecFind - find a definition by name

## SYNOPSIS

```c
#include <xspec.h>

const oncRPCXDef *oncRPCXSpecFind(const oncRPCXSpec *spec, const char *name);
```

## DESCRIPTION

`oncRPCXSpecFind()` returns the top-level definition called `name`: a
constant, a type (typedef, enum, struct or union) or a program. Enum items
are not definitions of their own; evaluate them with
[oncRPCXSpecEval(3)](oncRPCXSpecEval.3.md).

## RETURN VALUE

The definition, valid until the specification is freed, or `NULL` if
there is none or an argument is `NULL`.

## EXAMPLE

Print the fields of a structure:

```c
#include <stdio.h>
#include <xspec.h>

void print_struct(const oncRPCXSpec *spec, const char *name)
{
    const oncRPCXDef *d = oncRPCXSpecFind(spec, name);
    size_t i;

    if (d == NULL || d->kind != ONCRPC_XDEF_STRUCT) {
        printf("%s is not a structure\n", name);
        return;
    }
    for (i = 0; i < d->nfields; i++)
        printf("%s.%s (line %d)\n", name, d->fields[i].name, d->fields[i].line);
}
```

## SEE ALSO

[oncRPCXSpecParse(3)](oncRPCXSpecParse.3.md),
[oncRPCXSpecEval(3)](oncRPCXSpecEval.3.md)
