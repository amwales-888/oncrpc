/*
 * oncrpcgen.c - command line front end: generates C client/server stubs
 * and XDR routines from a .x specification.
 *
 * This is an application of liboncrpc, not part of the library: it does
 * ordinary blocking file I/O and supplies a stdlib-based allocator.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xdr.h"
#include "xspec.h"

#define ONCRPC_GEN_VERSION "2.0.0"

static void *app_realloc(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    return realloc(ptr, size);
}

static void app_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static const oncRPCAllocator app_alloc = { app_realloc, app_free, NULL };

/* Read a whole file into a NUL-terminated buffer. */
static char *read_file(const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    size_t len = 0, cap = 0, n;

    if (f == NULL) {
        snprintf(err, errlen, "%s: cannot open: %s", path, strerror(errno));
        return NULL;
    }
    for (;;) {
        if (cap - len < 4096) {
            char *nb;
            cap = cap ? cap * 2 : 16384;
            nb = (char *)realloc(buf, cap + 1);
            if (nb == NULL) {
                snprintf(err, errlen, "%s: out of memory", path);
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
        }
        n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0)
            break;
    }
    if (ferror(f)) {
        snprintf(err, errlen, "%s: read error", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[len] = '\0';
    if (strlen(buf) != len) {
        snprintf(err, errlen, "%s: file contains NUL bytes", path);
        free(buf);
        return NULL;
    }
    return buf;
}

typedef struct output {
    const char       *flag;
    oncRPCXGenKind    kind;
    const char       *suffix;
    int               skeleton;
    const char       *desc;
} output;

static const output outputs[] = {
    { "-h", ONCRPC_GEN_HEADER,          ".h",              0, "header (types, constants, prototypes)" },
    { "-c", ONCRPC_GEN_XDR,             "_xdr.c",          0, "XDR routines" },
    { "-l", ONCRPC_GEN_CLIENT,          "_clnt.c",         0, "client stubs" },
    { "-s", ONCRPC_GEN_SERVER,          "_svc.c",          0, "server dispatch and registration" },
    { "-S", ONCRPC_GEN_SERVER_SKELETON, "_server_skel.c",  1, "server implementation template" },
    { "-C", ONCRPC_GEN_CLIENT_SKELETON, "_client_skel.c",  1, "example client program" },
};
#define ONCRPC_GEN_NOUTPUTS (sizeof(outputs) / sizeof(outputs[0]))

static void usage(FILE *f)
{
    size_t i;
    fprintf(f,
            "usage: oncrpcgen [options] file.x\n"
            "\n"
            "Generate C code for the oncRPC library from an XDR/RPC specification.\n"
            "Without output options, -a is assumed.\n"
            "\n"
            "  -a        generate header, XDR routines, client stubs and server dispatch\n");
    for (i = 0; i < ONCRPC_GEN_NOUTPUTS; i++)
        fprintf(f, "  %s        generate the %s (<base>%s)\n", outputs[i].flag, outputs[i].desc,
                outputs[i].suffix);
    fprintf(f,
            "  -o DIR    write output files into DIR (default: current directory)\n"
            "  -O FILE   write a single output to FILE ('-' for standard output)\n"
            "  -b NAME   base name for output files and the header include\n"
            "            (default: input file name without directory and .x)\n"
            "  -f        overwrite existing skeleton files\n"
            "  --help    show this help\n"
            "  --version show the version\n");
}

static char *derive_base(const char *path)
{
    const char *s = path, *p;
    size_t n;
    char *b;

    for (p = path; *p != '\0'; p++)
        if (*p == '/' || *p == '\\')
            s = p + 1;
    n = strlen(s);
    if (n > 2 && strcmp(s + n - 2, ".x") == 0)
        n -= 2;
    b = (char *)malloc(n + 1);
    if (b != NULL) {
        memcpy(b, s, n);
        b[n] = '\0';
    }
    return b;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    int want[ONCRPC_GEN_NOUTPUTS];
    const char *outdir = NULL, *outfile = NULL, *input = NULL, *basearg = NULL;
    int force = 0, any = 0, count = 0, rc = 0, i;
    size_t k;
    char err[1024];
    char *base;
    oncRPCXSpec *spec;

    memset(want, 0, sizeof(want));
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int matched = 0;
        if (strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(a, "--version") == 0) {
            printf("oncrpcgen %s\n", ONCRPC_GEN_VERSION);
            return 0;
        }
        if (strcmp(a, "-a") == 0) {
            want[0] = want[1] = want[2] = want[3] = 1;
            any = 1;
            continue;
        }
        for (k = 0; k < ONCRPC_GEN_NOUTPUTS; k++) {
            if (strcmp(a, outputs[k].flag) == 0) {
                want[k] = 1;
                any = matched = 1;
            }
        }
        if (matched)
            continue;
        if (strcmp(a, "-f") == 0) {
            force = 1;
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "-O") == 0 || strcmp(a, "-b") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "oncrpcgen: %s needs an argument\n", a);
                return 2;
            }
            if (a[1] == 'o')
                outdir = argv[++i];
            else if (a[1] == 'O')
                outfile = argv[++i];
            else
                basearg = argv[++i];
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "oncrpcgen: unknown option '%s'\n", a);
            usage(stderr);
            return 2;
        } else if (input == NULL) {
            input = a;
        } else {
            fprintf(stderr, "oncrpcgen: only one input file may be given\n");
            return 2;
        }
    }
    if (input == NULL) {
        usage(stderr);
        return 2;
    }
    if (!any)
        want[0] = want[1] = want[2] = want[3] = 1;
    for (k = 0; k < ONCRPC_GEN_NOUTPUTS; k++)
        count += want[k];
    if (outfile != NULL && count != 1) {
        fprintf(stderr, "oncrpcgen: -O requires exactly one output option\n");
        return 2;
    }

    {
        char *text = read_file(input, err, sizeof(err));
        spec = text != NULL ? oncRPCXSpecParse(&app_alloc, text, input, err, sizeof(err)) : NULL;
        free(text);
    }
    if (spec == NULL) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    base = basearg != NULL ? NULL : derive_base(input);
    if (basearg == NULL && base == NULL) {
        fprintf(stderr, "oncrpcgen: out of memory\n");
        oncRPCXSpecFree(spec);
        return 1;
    }

    for (k = 0; k < ONCRPC_GEN_NOUTPUTS && rc == 0; k++) {
        const char *b = basearg != NULL ? basearg : base;
        char path[4096];
        FILE *f;
        int to_stdout;

        if (!want[k])
            continue;
        if (outfile != NULL)
            snprintf(path, sizeof(path), "%s", outfile);
        else if (outdir != NULL)
            snprintf(path, sizeof(path), "%s/%s%s", outdir, b, outputs[k].suffix);
        else
            snprintf(path, sizeof(path), "%s%s", b, outputs[k].suffix);
        to_stdout = strcmp(path, "-") == 0;

        if (outputs[k].skeleton && !force && !to_stdout && file_exists(path)) {
            fprintf(stderr, "oncrpcgen: %s exists; not overwriting (use -f)\n", path);
            rc = 1;
            break;
        }
        f = to_stdout ? stdout : fopen(path, "w");
        if (f == NULL) {
            fprintf(stderr, "oncrpcgen: cannot write %s\n", path);
            rc = 1;
            break;
        }
        {
            char *text = NULL;
            size_t len = 0;
            if (oncRPCXSpecGenerate(spec, outputs[k].kind, b, &text, &len, err, sizeof(err)) != 0) {
                fprintf(stderr, "oncrpcgen: %s: %s\n", input, err);
                rc = 1;
            } else if (fwrite(text, 1, len, f) != len) {
                fprintf(stderr, "oncrpcgen: error writing %s\n", path);
                rc = 1;
            }
            oncRPCFree(&app_alloc, text);
        }
        if (!to_stdout && fclose(f) != 0) {
            fprintf(stderr, "oncrpcgen: error writing %s\n", path);
            rc = 1;
        }
        if (rc != 0 && !to_stdout)
            remove(path);
    }

    free(base);
    oncRPCXSpecFree(spec);
    return rc;
}
