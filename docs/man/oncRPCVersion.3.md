# oncRPCVersion(3)

## NAME

oncRPCVersion - version of the library in use

## SYNOPSIS

```c
#include <oncrpc.h>

const char *oncRPCVersion(void);
```

## DESCRIPTION

`oncRPCVersion()` returns the version of the liboncrpc library the program
is running with, as text in the form `"major.minor.patch"`, for example
`"4.3.0"`.

The macro `ONCRPC_VERSION_STRING` holds the version of the header the
program was compiled with. With the shared library the two can differ:
the program may have been built against an older header than the library
installed now. Programs built against a given major version work with any
library of the same major version and an equal or newer minor version.

## RETURN VALUE

A pointer to a static string; never `NULL`.

## EXAMPLE

```c
#include <stdio.h>
#include <oncrpc.h>

void print_versions(void)
{
    printf("built with liboncrpc %s, running with %s\n",
           ONCRPC_VERSION_STRING, oncRPCVersion());
}
```

## SEE ALSO

[liboncrpc(3)](liboncrpc.3.md)
