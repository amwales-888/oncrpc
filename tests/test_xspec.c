/*
 * test_xspec.c - parser tests: accepted constructs and error reporting.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xspec.h"

/* Counting allocator: every block must be returned by the end. */
static long g_live;
static void *t_realloc(void *ctx, void *p, size_t n)
{
    void *q = realloc(p, n);
    (void)ctx;
    if (p == NULL && q != NULL)
        g_live++;
    return q;
}
static void t_free(void *ctx, void *p)
{
    (void)ctx;
    g_live--;
    free(p);
}
static const oncRPCAllocator A = { t_realloc, t_free, NULL };

static int g_fail, g_count;

#define CHECK(cond) do { g_count++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char *good =
    "/* comment */\n"
    "%#include <stdio.h>\n"
    "#define IGNORED 1\n"
    "const N = 0x10;           // comment\n"
    "const M = N;\n"
    "enum color { RED, GREEN = 5, BLUE };\n"
    "typedef struct { int a; } wrapped;\n"
    "struct s {\n"
    "    unsigned hyper big;\n"
    "    opaque id[N];\n"
    "    string name<>;\n"
    "    struct { int x; } inner;\n"
    "    s *next;\n"
    "};\n"
    "union u switch (color c) {\n"
    "case RED: case GREEN: int i;\n"
    "case BLUE: void;\n"
    "default: string other<M>;\n"
    "};\n"
    "program P { version V1 { void NOP(void) = 0; int F(s, int) = 1; } = 1;\n"
    "            version V2 { void NOP(void) = 0; string G(string) = 2; } = 2; } = 99;\n";

static void test_good(void)
{
    char err[256];
    oncRPCXSpec *spec = oncRPCXSpecParse(&A, good, "good.x", err, sizeof(err));
    const oncRPCXDef *d;
    int64_t v;

    CHECK(spec != NULL);
    if (spec == NULL) {
        fprintf(stderr, "  %s\n", err);
        return;
    }
    CHECK(spec->defs->kind == ONCRPC_XDEF_PASSTHROUGH &&
          strcmp(spec->defs->value, "#include <stdio.h>") == 0);
    CHECK(oncRPCXSpecEval(spec, "M", &v) == 0 && v == 16);
    CHECK(oncRPCXSpecEval(spec, "RED", &v) == 0 && v == 0);
    CHECK(oncRPCXSpecEval(spec, "BLUE", &v) == 0 && v == 6);
    CHECK(oncRPCXSpecEval(spec, "-0x10", &v) == 0 && v == -16);
    CHECK(oncRPCXSpecEval(spec, "nothing", &v) != 0);

    d = oncRPCXSpecFind(spec, "wrapped");
    CHECK(d != NULL && d->kind == ONCRPC_XDEF_STRUCT);   /* typedef struct {} hoisted */
    d = oncRPCXSpecFind(spec, "s_inner");
    CHECK(d != NULL && d->kind == ONCRPC_XDEF_STRUCT);   /* inline struct named */
    d = oncRPCXSpecFind(spec, "s");
    CHECK(d != NULL && d->nfields == 5);
    if (d != NULL && d->nfields == 5) {
        CHECK(d->fields[0].type.kind == ONCRPC_XTYPE_UHYPER);
        CHECK(d->fields[1].kind == ONCRPC_XDECL_OPAQUE_FIXED && strcmp(d->fields[1].size, "N") == 0);
        CHECK(d->fields[2].kind == ONCRPC_XDECL_STRING && d->fields[2].size == NULL);
        CHECK(d->fields[3].type.kind == ONCRPC_XTYPE_NAMED &&
              strcmp(d->fields[3].type.name, "s_inner") == 0);
        CHECK(d->fields[4].kind == ONCRPC_XDECL_OPTIONAL);
    }
    d = oncRPCXSpecFind(spec, "u");
    CHECK(d != NULL && d->ncases == 2 && d->cases[0].nvalues == 2 && d->defaultArm != NULL);
    d = oncRPCXSpecFind(spec, "P");
    CHECK(d != NULL && d->nversions == 2 && d->versions[0].procs[1].nargs == 2 &&
          d->versions[1].procs[1].result.kind == ONCRPC_XTYPE_STRING);
    oncRPCXSpecFree(spec);
}

static void expect_error(const char *src, const char *needle)
{
    char err[256];
    oncRPCXSpec *spec = oncRPCXSpecParse(&A, src, "bad.x", err, sizeof(err));
    g_count++;
    if (spec != NULL || strstr(err, needle) == NULL) {
        g_fail++;
        fprintf(stderr, "expected error containing '%s', got '%s'\n", needle,
                spec ? "(success)" : err);
    }
    oncRPCXSpecFree(spec);
}

static void test_errors(void)
{
    expect_error("const A = 1;\nconst A = 2;", "bad.x:2: error: 'A' is already defined");
    expect_error("struct s { int a; int a; };", "duplicate member 'a'");
    expect_error("struct s { foo a; };", "undefined type 'foo'");
    expect_error("struct s { int a }", "expected ';' before '}'");
    expect_error("struct s { quadruple q; };", "'quadruple' is not supported");
    expect_error("typedef void v;", "cannot typedef 'void'");
    expect_error("union u switch (string s<>) { case 1: void; };", "discriminant");
    expect_error("/* never closed", "unterminated comment");
    expect_error("const X = 0x;", "malformed hexadecimal");
    expect_error("const int = 4;", "reserved word");
    expect_error("program P { version V { void A(void) = 1; void A(int) = 2; } = 1; } = 1;",
                 "defined twice");
    expect_error("struct s { int a; };\n\n$", "bad.x:3: error: unexpected character '$'");
    expect_error("program P { version V { void A(void, int) = 1; } = 1; } = 1;", "expected ')'");
}

int main(void)
{
    test_good();
    test_errors();
    g_count++;
    if (g_live != 0) {
        g_fail++;
        fprintf(stderr, "leaked %ld blocks\n", g_live);
    }
    printf("test_xspec: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
