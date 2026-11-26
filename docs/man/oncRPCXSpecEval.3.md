# oncRPCXSpecEval(3)

## NAME

oncRPCXSpecEval - value of a constant, enum item or number

## SYNOPSIS

```c
#include <xspec.h>

int oncRPCXSpecEval(const oncRPCXSpec *spec, const char *value, int64_t *out);
```

## DESCRIPTION

Wherever the language allows a number - an array bound, an enum value, a
program, version or procedure number, a union case - the parse tree keeps
the text as written, which may be a numeric literal (decimal, hexadecimal
or octal, possibly negative), the name of a constant, the name of an enum
item, or `TRUE` or `FALSE`. `oncRPCXSpecEval()` works out its value and stores it in
`*out`, following constants defined in terms of other constants.

## RETURN VALUE

0 on success; -1 if the text is neither a number nor a known name, a
definition refers to itself, or an argument is `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <xspec.h>

void print_program_number(const oncRPCXSpec *spec, const char *program)
{
    const oncRPCXDef *d = oncRPCXSpecFind(spec, program);
    int64_t number;

    if (d != NULL && d->kind == ONCRPC_XDEF_PROGRAM &&
        oncRPCXSpecEval(spec, d->value, &number) == 0)
        printf("%s = 0x%llx\n", program, (unsigned long long)number);
}
```

## SEE ALSO

[oncRPCXSpecFind(3)](oncRPCXSpecFind.3.md),
[oncRPCXSpecParse(3)](oncRPCXSpecParse.3.md)
