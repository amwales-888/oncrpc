# oncRPCXSpecFree(3)

## NAME

oncRPCXSpecFree - release a parsed specification

## SYNOPSIS

```c
#include <xspec.h>

void oncRPCXSpecFree(oncRPCXSpec *spec);
```

## DESCRIPTION

`oncRPCXSpecFree()` releases everything
[oncRPCXSpecParse(3)](oncRPCXSpecParse.3.md) allocated for `spec`, the
structure included. Pointers into the tree (definitions, names, strings)
become invalid. Text produced by
[oncRPCXSpecGenerate(3)](oncRPCXSpecGenerate.3.md) is separate and
remains valid. A `NULL` specification is ignored.

## EXAMPLE

```c
#include <xspec.h>

int is_valid_spec(const oncRPCAllocator *a, const char *text)
{
    oncRPCXSpec *spec = oncRPCXSpecParse(a, text, "input.x", NULL, 0);
    int ok = spec != NULL;
    oncRPCXSpecFree(spec);
    return ok;
}
```

## SEE ALSO

[oncRPCXSpecParse(3)](oncRPCXSpecParse.3.md)
