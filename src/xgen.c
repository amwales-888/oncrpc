/*
 * xgen.c - C code generator for parsed .x specifications.
 *
 * Generated names follow the library's style: "oncRPC", then the .x name
 * in CamelCase (see camel()), then the version and role. For a type T,
 * program P, procedure X and version number N:
 *   int  oncRPCXdrT(oncRPCXdr *, T *)                   XDR routine
 *   void oncRPCXdrFreeT(const oncRPCAllocator *, T *)   free decoded data
 *   oncRPCStat oncRPCXVN(oncRPCClient *, ..., cb, data)  client stub (asynchronous)
 *   void oncRPCXVNSvc(..., oncRPCSvcReq *)               server procedure to implement
 *   int  oncRPCXVNReply(oncRPCSvcReq *, ...)             send the procedure's reply
 *   oncRPCStat oncRPCPVNRegister(srv, ctx)               server registration
 *   oncRPCXVNArgs                                        argument struct when X takes
 *                                                        more than one argument
 * e.g. CALC_ADD in version 1 gives oncRPCCalcAddV1, oncRPCCalcAddV1Svc, ...
 * The type names, constants and program, version and procedure numbers
 * keep the names they have in the .x file.
 */
#include "xspec.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ONCRPC_NAMELEN 512

typedef struct gen {
    const oncRPCAllocator *alloc;
    char               *out;
    size_t              outlen;
    size_t              outcap;
    const oncRPCXSpec *spec;
    const char         *base;
    char              **keys;
    size_t              nkeys;
    char               *err;
    size_t              errlen;
    int                 failed;
} gen;

/* Append formatted text to the output buffer. */
static void P(gen *g, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (g->failed)
        return;
    va_start(ap, fmt);
    n = vsnprintf(g->out + g->outlen, g->outcap - g->outlen, fmt, ap);
    va_end(ap);
    if (n < 0) {
        g->failed = 1;
        return;
    }
    if ((size_t)n >= g->outcap - g->outlen) {
        size_t nc = g->outcap * 2;
        char *nb;
        while (nc - g->outlen <= (size_t)n)
            nc *= 2;
        nb = (char *)oncRPCRealloc(g->alloc, g->out, nc);
        if (nb == NULL) {
            g->failed = 1;
            return;
        }
        g->out = nb;
        g->outcap = nc;
        va_start(ap, fmt);
        n = vsnprintf(g->out + g->outlen, g->outcap - g->outlen, fmt, ap);
        va_end(ap);
        if (n < 0 || (size_t)n >= g->outcap - g->outlen) {
            g->failed = 1;
            return;
        }
    }
    g->outlen += (size_t)n;
}

static void ind(gen *g, int n)
{
    while (n-- > 0)
        P(g, "    ");
}

static void gerr(gen *g, const char *fmt, ...)
{
    va_list ap;
    if (g->failed)
        return;
    g->failed = 1;
    if (g->err != NULL && g->errlen > 0) {
        va_start(ap, fmt);
        vsnprintf(g->err, g->errlen, fmt, ap);
        va_end(ap);
    }
}

/* snprintf that reports truncation as a generation error. */
static void sfmt(gen *g, char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t)r >= n)
        gerr(g, "identifier too long");
}

/* ---- Names ------------------------------------------------------------------ */

/*
 * The CamelCase form of a .x identifier: words split at '_', each starting
 * with a capital letter. A word written all in capitals keeps only its first
 * capital (CALC_ADD -> CalcAdd); other words keep their own case after the
 * first letter (calc_args -> CalcArgs, myType -> MyType).
 */
static void camel(gen *g, char *buf, size_t n, const char *src)
{
    const char *p = src;
    size_t j = 0;

    while (*p != '\0') {
        const char *w;
        size_t len, k;
        int upper = 1;

        while (*p == '_')
            p++;
        w = p;
        while (*p != '\0' && *p != '_')
            p++;
        len = (size_t)(p - w);
        for (k = 0; k < len; k++)
            if (islower((unsigned char)w[k]))
                upper = 0;
        for (k = 0; k < len; k++) {
            int c = (unsigned char)w[k];
            if (k == 0)
                c = toupper(c);
            else if (upper)
                c = tolower(c);
            if (j + 1 >= n) {
                gerr(g, "identifier too long");
                buf[0] = '\0';
                return;
            }
            buf[j++] = (char)c;
        }
    }
    buf[j] = '\0';
    if (j == 0)
        gerr(g, "cannot form a C name from '%s'", src);
}

/* The part of generated names that stands for type t. Argument structs the
 * generator makes itself are already named oncRPC...; .x names may not start
 * with oncRPC (check_names). */
static void type_part(gen *g, char *buf, size_t n, const char *name)
{
    if (strncmp(name, "oncRPC", 6) == 0)
        sfmt(g, buf, n, "%s", name + 6);
    else
        camel(g, buf, n, name);
}

/* oncRPCXdrT */
static void xdr_fn(gen *g, char *buf, size_t n, const char *type)
{
    char part[ONCRPC_NAMELEN];
    type_part(g, part, sizeof(part), type);
    sfmt(g, buf, n, "oncRPCXdr%s", part);
}

/* oncRPCXdrFreeT */
static void free_fn(gen *g, char *buf, size_t n, const char *type)
{
    char part[ONCRPC_NAMELEN];
    type_part(g, part, sizeof(part), type);
    sfmt(g, buf, n, "oncRPCXdrFree%s", part);
}

/* ---- Type mapping --------------------------------------------------------- */

static const char *base_ctype(oncRPCXTypeKind k)
{
    switch (k) {
    case ONCRPC_XTYPE_INT:    return "int32_t";
    case ONCRPC_XTYPE_UINT:   return "uint32_t";
    case ONCRPC_XTYPE_HYPER:  return "int64_t";
    case ONCRPC_XTYPE_UHYPER: return "uint64_t";
    case ONCRPC_XTYPE_FLOAT:  return "float";
    case ONCRPC_XTYPE_DOUBLE: return "double";
    case ONCRPC_XTYPE_BOOL:   return "oncRPCBool";
    case ONCRPC_XTYPE_CHAR:   return "int8_t";
    case ONCRPC_XTYPE_UCHAR:  return "uint8_t";
    case ONCRPC_XTYPE_SHORT:  return "int16_t";
    case ONCRPC_XTYPE_USHORT: return "uint16_t";
    case ONCRPC_XTYPE_STRING: return "char *";
    default:                  return "void";
    }
}

static const char *base_xfn(oncRPCXTypeKind k)
{
    switch (k) {
    case ONCRPC_XTYPE_INT:    return "oncRPCXdrInt32";
    case ONCRPC_XTYPE_UINT:   return "oncRPCXdrUint32";
    case ONCRPC_XTYPE_HYPER:  return "oncRPCXdrInt64";
    case ONCRPC_XTYPE_UHYPER: return "oncRPCXdrUint64";
    case ONCRPC_XTYPE_FLOAT:  return "oncRPCXdrFloat";
    case ONCRPC_XTYPE_DOUBLE: return "oncRPCXdrDouble";
    case ONCRPC_XTYPE_BOOL:   return "oncRPCXdrBool";
    case ONCRPC_XTYPE_CHAR:   return "oncRPCXdrInt8";
    case ONCRPC_XTYPE_UCHAR:  return "oncRPCXdrUint8";
    case ONCRPC_XTYPE_SHORT:  return "oncRPCXdrInt16";
    case ONCRPC_XTYPE_USHORT: return "oncRPCXdrUint16";
    default:                  return "oncRPCXdrVoid";
    }
}

static const char *ctype(const oncRPCXType *t)
{
    return t->kind == ONCRPC_XTYPE_NAMED ? t->name : base_ctype(t->kind);
}

/* "T name" (handles "char *name") */
static void print_var(gen *g, const oncRPCXType *t, const char *name)
{
    if (t->kind == ONCRPC_XTYPE_STRING)
        P(g, "char *%s", name);
    else
        P(g, "%s %s", ctype(t), name);
}

static void print_res_ptr(gen *g, const oncRPCXType *t)
{
    if (t->kind == ONCRPC_XTYPE_STRING)
        P(g, "char **");
    else
        P(g, "%s *", ctype(t));
}

/* Prints the XDR call for type t on pointer expression ptr. */
static void print_xdr_call(gen *g, const oncRPCXType *t, const char *ptr)
{
    char fn[ONCRPC_NAMELEN + 16];
    if (t->kind == ONCRPC_XTYPE_NAMED) {
        xdr_fn(g, fn, sizeof(fn), t->name);
        P(g, "%s(xdrs, %s)", fn, ptr);
    }
    else if (t->kind == ONCRPC_XTYPE_STRING)
        P(g, "oncRPCXdrString(xdrs, %s, ONCRPC_XDR_UNBOUNDED)", ptr);
    else
        P(g, "%s(xdrs, %s)", base_xfn(t->kind), ptr);
}

static void bound_expr(const oncRPCXDecl *d, char *buf, size_t n)
{
    if (d->size != NULL)
        snprintf(buf, n, "(uint32_t)(%s)", d->size);
    else
        snprintf(buf, n, "ONCRPC_XDR_UNBOUNDED");
}

/* ---- Declarations in headers ------------------------------------------------ */

static void emit_field(gen *g, const oncRPCXDecl *d, int n)
{
    const char *ct = ctype(&d->type);
    switch (d->kind) {
    case ONCRPC_XDECL_VOID:
        return;
    case ONCRPC_XDECL_PLAIN:
        ind(g, n);
        P(g, "%s %s;\n", ct, d->name);
        break;
    case ONCRPC_XDECL_FIXED_ARRAY:
        ind(g, n);
        P(g, "%s %s[%s];\n", ct, d->name, d->size);
        break;
    case ONCRPC_XDECL_VAR_ARRAY:
        ind(g, n);
        P(g, "struct {\n");
        ind(g, n + 1);
        P(g, "uint32_t %s_len;\n", d->name);
        ind(g, n + 1);
        P(g, "%s *%s_val;\n", ct, d->name);
        ind(g, n);
        P(g, "} %s;\n", d->name);
        break;
    case ONCRPC_XDECL_OPAQUE_FIXED:
        ind(g, n);
        P(g, "uint8_t %s[%s];\n", d->name, d->size);
        break;
    case ONCRPC_XDECL_OPAQUE_VAR:
        ind(g, n);
        P(g, "struct {\n");
        ind(g, n + 1);
        P(g, "uint32_t %s_len;\n", d->name);
        ind(g, n + 1);
        P(g, "uint8_t *%s_val;\n", d->name);
        ind(g, n);
        P(g, "} %s;\n", d->name);
        break;
    case ONCRPC_XDECL_STRING:
        ind(g, n);
        P(g, "char *%s;\n", d->name);
        break;
    case ONCRPC_XDECL_OPTIONAL:
        ind(g, n);
        P(g, "%s *%s;\n", ct, d->name);
        break;
    }
}

static void emit_typedef(gen *g, const oncRPCXDef *def)
{
    const oncRPCXDecl *d = &def->decl;
    const char *ct = ctype(&d->type), *nm = def->name;

    switch (d->kind) {
    case ONCRPC_XDECL_PLAIN:
        P(g, "typedef %s %s;\n", ct, nm);
        break;
    case ONCRPC_XDECL_FIXED_ARRAY:
        P(g, "typedef %s %s[%s];\n", ct, nm, d->size);
        break;
    case ONCRPC_XDECL_VAR_ARRAY:
        P(g, "typedef struct {\n    uint32_t %s_len;\n    %s *%s_val;\n} %s;\n", nm, ct, nm, nm);
        break;
    case ONCRPC_XDECL_OPAQUE_FIXED:
        P(g, "typedef uint8_t %s[%s];\n", nm, d->size);
        break;
    case ONCRPC_XDECL_OPAQUE_VAR:
        P(g, "typedef struct {\n    uint32_t %s_len;\n    uint8_t *%s_val;\n} %s;\n", nm, nm, nm);
        break;
    case ONCRPC_XDECL_STRING:
        P(g, "typedef char *%s;\n", nm);
        break;
    case ONCRPC_XDECL_OPTIONAL:
        P(g, "typedef %s *%s;\n", ct, nm);
        break;
    case ONCRPC_XDECL_VOID:
        break;
    }
}

/* ---- XDR routine bodies ------------------------------------------------------ */

/* Emit code that runs the XDR routine for declaration d stored at lvalue lv. */
static void emit_decl_xdr(gen *g, const oncRPCXDecl *d, const char *lv, int n)
{
    char ptr[ONCRPC_NAMELEN * 2], len[ONCRPC_NAMELEN * 2], val[ONCRPC_NAMELEN * 2], bound[ONCRPC_NAMELEN];
    const char *ct = ctype(&d->type);

    bound_expr(d, bound, sizeof(bound));
    switch (d->kind) {
    case ONCRPC_XDECL_VOID:
        return;

    case ONCRPC_XDECL_PLAIN:
        sfmt(g, ptr, sizeof(ptr), "&%s", lv);
        ind(g, n);
        P(g, "if (!");
        print_xdr_call(g, &d->type, ptr);
        P(g, ")\n");
        ind(g, n + 1);
        P(g, "return 0;\n");
        return;

    case ONCRPC_XDECL_FIXED_ARRAY:
        sfmt(g, ptr, sizeof(ptr), "&%s[i_]", lv);
        ind(g, n);
        P(g, "{\n");
        ind(g, n + 1);
        P(g, "uint32_t i_;\n");
        ind(g, n + 1);
        P(g, "for (i_ = 0; i_ < (uint32_t)(%s); i_++) {\n", d->size);
        ind(g, n + 2);
        P(g, "if (!");
        print_xdr_call(g, &d->type, ptr);
        P(g, ")\n");
        ind(g, n + 3);
        P(g, "return 0;\n");
        ind(g, n + 1);
        P(g, "}\n");
        ind(g, n);
        P(g, "}\n");
        return;

    case ONCRPC_XDECL_VAR_ARRAY:
        sfmt(g, len, sizeof(len), "%s.%s_len", lv, d->name);
        sfmt(g, val, sizeof(val), "%s.%s_val", lv, d->name);
        sfmt(g, ptr, sizeof(ptr), "&%s[i_]", val);
        ind(g, n);     P(g, "{\n");
        ind(g, n + 1); P(g, "uint32_t i_;\n");
        ind(g, n + 1); P(g, "if (xdrs->op == ONCRPC_XDR_ENCODE && %s == NULL && %s != 0)\n", val, len);
        ind(g, n + 2); P(g, "return 0;\n");
        ind(g, n + 1); P(g, "if (!oncRPCXdrArrayLen(xdrs, &%s, %s))\n", len, bound);
        ind(g, n + 2); P(g, "return 0;\n");
        ind(g, n + 1); P(g, "if (xdrs->op == ONCRPC_XDR_DECODE) {\n");
        ind(g, n + 2); P(g, "%s = NULL;\n", val);
        ind(g, n + 2); P(g, "if (%s != 0) {\n", len);
        ind(g, n + 3); P(g, "%s = (%s *)oncRPCXdrArrayAlloc(xdrs, %s, sizeof(%s));\n",
                         val, ct, len, ct);
        ind(g, n + 3); P(g, "if (%s == NULL) {\n", val);
        ind(g, n + 4); P(g, "%s = 0;\n", len);
        ind(g, n + 4); P(g, "return 0;\n");
        ind(g, n + 3); P(g, "}\n");
        ind(g, n + 2); P(g, "}\n");
        ind(g, n + 1); P(g, "}\n");
        ind(g, n + 1); P(g, "if (%s != NULL) {\n", val);
        ind(g, n + 2); P(g, "for (i_ = 0; i_ < %s; i_++) {\n", len);
        ind(g, n + 3); P(g, "if (!");
        print_xdr_call(g, &d->type, ptr);
        P(g, ")\n");
        ind(g, n + 4); P(g, "return 0;\n");
        ind(g, n + 2); P(g, "}\n");
        ind(g, n + 1); P(g, "}\n");
        ind(g, n + 1); P(g, "if (xdrs->op == ONCRPC_XDR_FREE) {\n");
        ind(g, n + 2); P(g, "oncRPCXdrMemFree(xdrs, %s);\n", val);
        ind(g, n + 2); P(g, "%s = NULL;\n", val);
        ind(g, n + 2); P(g, "%s = 0;\n", len);
        ind(g, n + 1); P(g, "}\n");
        ind(g, n);     P(g, "}\n");
        return;

    case ONCRPC_XDECL_OPAQUE_FIXED:
        ind(g, n);
        P(g, "if (!oncRPCXdrOpaque(xdrs, %s, (uint32_t)(%s)))\n", lv, d->size);
        ind(g, n + 1);
        P(g, "return 0;\n");
        return;

    case ONCRPC_XDECL_OPAQUE_VAR:
        ind(g, n);
        P(g, "if (!oncRPCXdrBytes(xdrs, &%s.%s_val, &%s.%s_len, %s))\n",
          lv, d->name, lv, d->name, bound);
        ind(g, n + 1);
        P(g, "return 0;\n");
        return;

    case ONCRPC_XDECL_STRING:
        ind(g, n);
        P(g, "if (!oncRPCXdrString(xdrs, &%s, %s))\n", lv, bound);
        ind(g, n + 1);
        P(g, "return 0;\n");
        return;

    case ONCRPC_XDECL_OPTIONAL:
        ind(g, n);     P(g, "{\n");
        ind(g, n + 1); P(g, "oncRPCBool more_ = (%s != NULL);\n", lv);
        ind(g, n + 1); P(g, "if (xdrs->op != ONCRPC_XDR_FREE && !oncRPCXdrBool(xdrs, &more_))\n");
        ind(g, n + 2); P(g, "return 0;\n");
        ind(g, n + 1); P(g, "if (xdrs->op == ONCRPC_XDR_DECODE) {\n");
        ind(g, n + 2); P(g, "%s = NULL;\n", lv);
        ind(g, n + 2); P(g, "if (more_) {\n");
        ind(g, n + 3); P(g, "%s = (%s *)oncRPCXdrArrayAlloc(xdrs, 1, sizeof(%s));\n", lv, ct, ct);
        ind(g, n + 3); P(g, "if (%s == NULL)\n", lv);
        ind(g, n + 4); P(g, "return 0;\n");
        ind(g, n + 2); P(g, "}\n");
        ind(g, n + 1); P(g, "}\n");
        ind(g, n + 1); P(g, "if (%s != NULL && !", lv);
        print_xdr_call(g, &d->type, lv);
        P(g, ")\n");
        ind(g, n + 2); P(g, "return 0;\n");
        ind(g, n + 1); P(g, "if (xdrs->op == ONCRPC_XDR_FREE && %s != NULL) {\n", lv);
        ind(g, n + 2); P(g, "oncRPCXdrMemFree(xdrs, %s);\n", lv);
        ind(g, n + 2); P(g, "%s = NULL;\n", lv);
        ind(g, n + 1); P(g, "}\n");
        ind(g, n);     P(g, "}\n");
        return;
    }
}

/* TRUE and FALSE are the XDR bool constants; C has no such names. */
static const char *case_label(gen *g, const char *v)
{
    if (oncRPCXSpecFind(g->spec, v) == NULL) {
        if (strcmp(v, "TRUE") == 0)
            return "1";
        if (strcmp(v, "FALSE") == 0)
            return "0";
    }
    return v;
}

static int is_type_def(const oncRPCXDef *d)
{
    return d->kind == ONCRPC_XDEF_TYPEDEF || d->kind == ONCRPC_XDEF_ENUM ||
           d->kind == ONCRPC_XDEF_STRUCT || d->kind == ONCRPC_XDEF_UNION;
}

static void emit_free_fn(gen *g, const char *name)
{
    char xf[ONCRPC_NAMELEN + 16], ff[ONCRPC_NAMELEN + 16];
    xdr_fn(g, xf, sizeof(xf), name);
    free_fn(g, ff, sizeof(ff), name);
    P(g, "void %s(const oncRPCAllocator *alloc, %s *objp)\n{\n", ff, name);
    P(g, "    oncRPCXdr xdrs_;\n");
    P(g, "    oncRPCXdrInitFree(&xdrs_, alloc);\n");
    P(g, "    (void)%s(&xdrs_, objp);\n}\n\n", xf);
}

static void emit_type_xdr(gen *g, const oncRPCXDef *d)
{
    char lv[ONCRPC_NAMELEN * 2], xf[ONCRPC_NAMELEN + 16];
    size_t i, j;

    xdr_fn(g, xf, sizeof(xf), d->name);
    P(g, "int %s(oncRPCXdr *xdrs, %s *objp)\n{\n", xf, d->name);
    switch (d->kind) {
    case ONCRPC_XDEF_ENUM:
        P(g, "    int32_t v_ = (int32_t)*objp;\n");
        P(g, "    if (!oncRPCXdrEnum(xdrs, &v_))\n        return 0;\n");
        P(g, "    if (xdrs->op == ONCRPC_XDR_DECODE)\n        *objp = (%s)v_;\n", d->name);
        break;
    case ONCRPC_XDEF_TYPEDEF:
        emit_decl_xdr(g, &d->decl, "(*objp)", 1);
        break;
    case ONCRPC_XDEF_STRUCT:
        for (i = 0; i < d->nfields; i++) {
            sfmt(g, lv, sizeof(lv), "objp->%s", d->fields[i].name);
            emit_decl_xdr(g, &d->fields[i], lv, 1);
        }
        break;
    case ONCRPC_XDEF_UNION:
        sfmt(g, lv, sizeof(lv), "objp->%s", d->disc.name);
        emit_decl_xdr(g, &d->disc, lv, 1);
        P(g, "    switch (objp->%s) {\n", d->disc.name);
        for (i = 0; i < d->ncases; i++) {
            const oncRPCXCase *c = &d->cases[i];
            for (j = 0; j < c->nvalues; j++)
                P(g, "    case %s:\n", case_label(g, c->values[j]));
            if (c->arm.kind != ONCRPC_XDECL_VOID) {
                sfmt(g, lv, sizeof(lv), "objp->%s_u.%s", d->name, c->arm.name);
                emit_decl_xdr(g, &c->arm, lv, 2);
            }
            P(g, "        break;\n");
        }
        P(g, "    default:\n");
        if (d->defaultArm != NULL) {
            if (d->defaultArm->kind != ONCRPC_XDECL_VOID) {
                sfmt(g, lv, sizeof(lv), "objp->%s_u.%s", d->name, d->defaultArm->name);
                emit_decl_xdr(g, d->defaultArm, lv, 2);
            }
            P(g, "        break;\n");
        } else {
            P(g, "        return 0;\n");
        }
        P(g, "    }\n");
        break;
    default:
        break;
    }
    P(g, "    return 1;\n}\n\n");
    emit_free_fn(g, d->name);
}

/* ---- Programs --------------------------------------------------------------- */

static int version_number(gen *g, const oncRPCXVersion *v, char *buf, size_t n)
{
    int64_t val;
    if (oncRPCXSpecEval(g->spec, v->number, &val) != 0 || val < 0 || val > 0xffffffffLL) {
        gerr(g, "cannot evaluate version number '%s' of version '%s'", v->number, v->name);
        return -1;
    }
    sfmt(g, buf, n, "%lld", (long long)val);
    return 0;
}

/* "oncRPC" + CamelCase(name) + "V" + version number: the stem of the names
 * for a procedure (or, given the program's name, for a program version). */
static int proc_fn(gen *g, const char *name, const oncRPCXVersion *v, char *buf, size_t n)
{
    char vn[32], cc[ONCRPC_NAMELEN];
    if (version_number(g, v, vn, sizeof(vn)) != 0)
        return -1;
    camel(g, cc, sizeof(cc), name);
    sfmt(g, buf, n, "oncRPC%sV%s", cc, vn);
    return g->failed ? -1 : 0;
}

/* Type passed for the procedure's argument(s); 0 for void. */
static int proc_argtype(const oncRPCXProc *pr, const char *fn, oncRPCXType *t,
                        char *namebuf, size_t n)
{
    if (pr->nargs == 0)
        return 0;
    if (pr->nargs == 1) {
        *t = pr->args[0];
        return 1;
    }
    snprintf(namebuf, n, "%sArgs", fn);
    memset(t, 0, sizeof(*t));
    t->kind = ONCRPC_XTYPE_NAMED;
    t->name = namebuf;
    return 1;
}

/* "const char *" for strings, "const T *" otherwise: read-only arguments. */
static void print_arg_param(gen *g, const oncRPCXType *t, const char *name)
{
    if (t->kind == ONCRPC_XTYPE_STRING)
        P(g, "const char *%s", name);
    else
        P(g, "const %s *%s", ctype(t), name);
}

static void print_client_proto(gen *g, const oncRPCXProc *pr, const char *fn)
{
    size_t i;
    char an[16];
    P(g, "oncRPCStat %s(oncRPCClient *clnt", fn);
    for (i = 0; i < pr->nargs; i++) {
        if (pr->nargs == 1)
            sfmt(g, an, sizeof(an), "arg");
        else
            sfmt(g, an, sizeof(an), "arg%u", (unsigned)(i + 1));
        P(g, ", ");
        print_arg_param(g, &pr->args[i], an);
    }
    if (pr->result.kind != ONCRPC_XTYPE_VOID) {
        P(g, ", ");
        print_res_ptr(g, &pr->result);
        P(g, "res");
    }
    P(g, ", oncRPCCallCb cb, void *data)");
}

static void print_svc_proto(gen *g, const oncRPCXProc *pr, const char *fn)
{
    size_t i;
    char an[16];
    P(g, "void %sSvc(", fn);
    for (i = 0; i < pr->nargs; i++) {
        if (pr->nargs == 1)
            sfmt(g, an, sizeof(an), "arg");
        else
            sfmt(g, an, sizeof(an), "arg%u", (unsigned)(i + 1));
        print_arg_param(g, &pr->args[i], an);
        P(g, ", ");
    }
    P(g, "oncRPCSvcReq *req)");
}

static void print_reply_proto(gen *g, const oncRPCXProc *pr, const char *fn)
{
    P(g, "int %sReply(oncRPCSvcReq *req", fn);
    if (pr->result.kind != ONCRPC_XTYPE_VOID) {
        P(g, ", ");
        print_arg_param(g, &pr->result, "res");
    }
    P(g, ")");
}

/* Has an earlier version of the program already defined this proc name? */
static int proc_seen(const oncRPCXDef *prog, size_t vi, const char *name)
{
    size_t i, j;
    for (i = 0; i < vi; i++)
        for (j = 0; j < prog->versions[i].nprocs; j++)
            if (strcmp(prog->versions[i].procs[j].name, name) == 0)
                return 1;
    return 0;
}

static void header_program(gen *g, const oncRPCXDef *d)
{
    size_t vi, i, k;
    char fn[ONCRPC_NAMELEN], pfn[ONCRPC_NAMELEN];

    P(g, "\n/* ---- Program %s ---- */\n\n", d->name);
    P(g, "#define %s %s\n", d->name, d->value);
    for (vi = 0; vi < d->nversions; vi++) {
        const oncRPCXVersion *v = &d->versions[vi];
        P(g, "#define %s %s\n", v->name, v->number);
        for (i = 0; i < v->nprocs; i++)
            if (!proc_seen(d, vi, v->procs[i].name))
                P(g, "#define %s %s\n", v->procs[i].name, v->procs[i].number);
    }
    for (vi = 0; vi < d->nversions && !g->failed; vi++) {
        const oncRPCXVersion *v = &d->versions[vi];

        P(g, "\n/* Version %s */\n", v->name);
        for (i = 0; i < v->nprocs; i++) {
            const oncRPCXProc *pr = &v->procs[i];
            if (pr->nargs < 2 || proc_fn(g, pr->name, v, fn, sizeof(fn)) != 0)
                continue;
            {
                char at[ONCRPC_NAMELEN + 8], xf[ONCRPC_NAMELEN + 16], ff[ONCRPC_NAMELEN + 16];
                sfmt(g, at, sizeof(at), "%sArgs", fn);
                xdr_fn(g, xf, sizeof(xf), at);
                free_fn(g, ff, sizeof(ff), at);
                P(g, "\ntypedef struct %s {\n", at);
                for (k = 0; k < pr->nargs; k++) {
                    char an[16];
                    sfmt(g, an, sizeof(an), "arg%u", (unsigned)(k + 1));
                    P(g, "    ");
                    print_var(g, &pr->args[k], an);
                    P(g, ";\n");
                }
                P(g, "} %s;\n", at);
                P(g, "int %s(oncRPCXdr *xdrs, %s *objp);\n", xf, at);
                P(g, "void %s(const oncRPCAllocator *alloc, %s *objp);\n", ff, at);
            }
        }

        P(g, "\n/* Client stubs. Each starts a call and returns at once; cb runs when\n"
             " * it completes. res must stay valid until then; on ONCRPC_OK release\n"
             " * its contents with the matching oncRPCXdrFree* routine (oncRPCFree for\n"
             " * a string). */\n");
        for (i = 0; i < v->nprocs; i++) {
            if (proc_fn(g, v->procs[i].name, v, fn, sizeof(fn)) != 0)
                return;
            print_client_proto(g, &v->procs[i], fn);
            P(g, ";\n");
        }
        P(g, "\n/* Server procedures: implement these. Each must finish req exactly\n"
             " * once - immediately or later - with its ...Reply function or an\n"
             " * oncRPCSvcErr* function (or oncRPCSvcDrop). The arguments stay valid\n"
             " * until then. */\n");
        for (i = 0; i < v->nprocs; i++) {
            if (proc_fn(g, v->procs[i].name, v, fn, sizeof(fn)) != 0)
                return;
            print_svc_proto(g, &v->procs[i], fn);
            P(g, ";\n");
        }
        P(g, "\n/* Replies: encode the result (not retained) and finish req. */\n");
        for (i = 0; i < v->nprocs; i++) {
            if (proc_fn(g, v->procs[i].name, v, fn, sizeof(fn)) != 0)
                return;
            print_reply_proto(g, &v->procs[i], fn);
            P(g, ";\n");
        }
        if (proc_fn(g, d->name, v, pfn, sizeof(pfn)) != 0)
            return;
        P(g, "\n/* Serve version %s on srv (see oncRPCServerRegister). */\n", v->name);
        P(g, "oncRPCStat %sRegister(oncRPCServer *srv, void *ctx);\n", pfn);
    }
}

static void guard_name(const char *base, char *buf, size_t n)
{
    size_t i, j = 0;
    for (i = 0; base[i] != '\0' && j + 1 < n; i++) {
        unsigned char c = (unsigned char)base[i];
        buf[j++] = isalnum(c) ? (char)toupper(c) : '_';
    }
    buf[j] = '\0';
}

/* The input's file name without its directories, so that generated code is
 * the same on every system and does not record local paths. */
static const char *source_name(const gen *g)
{
    const char *name = g->spec->filename, *p;
    for (p = name; *p != '\0'; p++)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    return name;
}

static void file_banner(gen *g, const char *what)
{
    P(g, "/*\n * %s\n * Generated by oncrpcgen from %s. Do not edit.\n */\n\n",
      what, source_name(g));
}

/* Generated .c files include libuv through oncrpc.h, which on Linux needs
 * POSIX declarations under strict C99. */
static void c_prelude(gen *g)
{
    P(g, "/* glibc hides the POSIX declarations libuv needs under strict -std=c99. */\n"
         "#if defined(__linux__) && !defined(_POSIX_C_SOURCE)\n"
         "#define _POSIX_C_SOURCE 200809L\n"
         "#endif\n\n");
}

static void gen_header(gen *g)
{
    const oncRPCXDef *d;
    char guard[ONCRPC_NAMELEN];
    size_t i;

    guard_name(g->base, guard, sizeof(guard));
    file_banner(g, "Types, constants and prototypes");
    P(g, "#ifndef ONCRPC_GEN_%s_H\n#define ONCRPC_GEN_%s_H\n\n", guard, guard);
    {
        /* Without programs the code needs only the XDR layer (no libuv). */
        int has_program = 0;
        for (d = g->spec->defs; d != NULL; d = d->next)
            if (d->kind == ONCRPC_XDEF_PROGRAM)
                has_program = 1;
        P(g, "#include <stdint.h>\n#include \"%s\"\n\n", has_program ? "oncrpc.h" : "xdr.h");
    }
    P(g, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");

    for (d = g->spec->defs; d != NULL; d = d->next)
        if (d->kind == ONCRPC_XDEF_STRUCT || d->kind == ONCRPC_XDEF_UNION)
            P(g, "typedef struct %s %s;\n", d->name, d->name);
    P(g, "\n");

    for (d = g->spec->defs; d != NULL; d = d->next) {
        switch (d->kind) {
        case ONCRPC_XDEF_PASSTHROUGH:
            P(g, "%s\n", d->value);
            break;
        case ONCRPC_XDEF_CONST:
            P(g, "#define %s %s\n", d->name, d->value);
            break;
        case ONCRPC_XDEF_ENUM:
            P(g, "\ntypedef enum %s {\n", d->name);
            for (i = 0; i < d->nitems; i++) {
                if (d->items[i].value != NULL)
                    P(g, "    %s = %s", d->items[i].name, d->items[i].value);
                else
                    P(g, "    %s", d->items[i].name);
                P(g, "%s\n", i + 1 < d->nitems ? "," : "");
            }
            P(g, "} %s;\n", d->name);
            break;
        case ONCRPC_XDEF_STRUCT:
            P(g, "\nstruct %s {\n", d->name);
            for (i = 0; i < d->nfields; i++)
                emit_field(g, &d->fields[i], 1);
            P(g, "};\n");
            break;
        case ONCRPC_XDEF_UNION: {
            int any = 0;
            P(g, "\nstruct %s {\n", d->name);
            emit_field(g, &d->disc, 1);
            for (i = 0; i < d->ncases; i++)
                any |= d->cases[i].arm.kind != ONCRPC_XDECL_VOID;
            if (d->defaultArm != NULL && d->defaultArm->kind != ONCRPC_XDECL_VOID)
                any = 1;
            if (any) {
                P(g, "    union {\n");
                for (i = 0; i < d->ncases; i++)
                    emit_field(g, &d->cases[i].arm, 2);
                if (d->defaultArm != NULL)
                    emit_field(g, d->defaultArm, 2);
                P(g, "    } %s_u;\n", d->name);
            }
            P(g, "};\n");
            break;
        }
        case ONCRPC_XDEF_TYPEDEF:
            P(g, "\n");
            emit_typedef(g, d);
            break;
        default:
            break;
        }
    }

    P(g, "\n/* ---- XDR routines ---- */\n\n");
    for (d = g->spec->defs; d != NULL; d = d->next) {
        char xf[ONCRPC_NAMELEN + 16], ff[ONCRPC_NAMELEN + 16];
        if (!is_type_def(d))
            continue;
        xdr_fn(g, xf, sizeof(xf), d->name);
        free_fn(g, ff, sizeof(ff), d->name);
        P(g, "int %s(oncRPCXdr *xdrs, %s *objp);\n", xf, d->name);
        P(g, "void %s(const oncRPCAllocator *alloc, %s *objp);\n", ff, d->name);
    }

    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next)
        if (d->kind == ONCRPC_XDEF_PROGRAM)
            header_program(g, d);

    P(g, "\n#ifdef __cplusplus\n}\n#endif\n\n#endif /* ONCRPC_GEN_%s_H */\n", guard);
}

static void gen_xdr(gen *g)
{
    const oncRPCXDef *d;
    size_t vi, i, k;
    char fn[ONCRPC_NAMELEN], lv[ONCRPC_NAMELEN + 16];

    file_banner(g, "XDR routines");
    c_prelude(g);
    P(g, "#include \"%s.h\"\n\n", g->base);
    for (d = g->spec->defs; d != NULL; d = d->next)
        if (is_type_def(d))
            emit_type_xdr(g, d);

    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions; vi++) {
            const oncRPCXVersion *v = &d->versions[vi];
            for (i = 0; i < v->nprocs; i++) {
                const oncRPCXProc *pr = &v->procs[i];
                if (pr->nargs < 2)
                    continue;
                if (proc_fn(g, pr->name, v, fn, sizeof(fn)) != 0)
                    return;
                {
                    char at[ONCRPC_NAMELEN + 8], xf[ONCRPC_NAMELEN + 16];
                    sfmt(g, at, sizeof(at), "%sArgs", fn);
                    xdr_fn(g, xf, sizeof(xf), at);
                    P(g, "int %s(oncRPCXdr *xdrs, %s *objp)\n{\n", xf, at);
                }
                for (k = 0; k < pr->nargs; k++) {
                    sfmt(g, lv, sizeof(lv), "&objp->arg%u", (unsigned)(k + 1));
                    P(g, "    if (!");
                    print_xdr_call(g, &pr->args[k], lv);
                    P(g, ")\n        return 0;\n");
                }
                P(g, "    return 1;\n}\n\n");
                sfmt(g, lv, sizeof(lv), "%sArgs", fn);
                emit_free_fn(g, lv);
            }
        }
    }
}

/* ---- Generic-routine adapters for client and server files ------------------------ */

/* Name of the static adapter giving type t's XDR routine the generic
 * oncRPCXdrProc signature: oncRPCGen + the routine's name without "oncRPC"
 * (oncRPCGenXdrCalcArgs, oncRPCGenXdrInt32). A .x type cannot take a
 * built-in's name, as check_names rejects names the library uses. */
static void adapter_fn(gen *g, const oncRPCXType *t, char *buf, size_t n)
{
    char part[ONCRPC_NAMELEN];
    if (t->kind == ONCRPC_XTYPE_NAMED)
        type_part(g, part, sizeof(part), t->name);
    else if (t->kind == ONCRPC_XTYPE_STRING)
        sfmt(g, part, sizeof(part), "String");
    else
        sfmt(g, part, sizeof(part), "%s", base_xfn(t->kind) + 9);
    sfmt(g, buf, n, "oncRPCGenXdr%s", part);
}

static int have_key(gen *g, const char *key)
{
    size_t i;
    for (i = 0; i < g->nkeys; i++)
        if (strcmp(g->keys[i], key) == 0)
            return 1;
    return 0;
}

static void emit_adapter(gen *g, const oncRPCXType *t)
{
    char ptr[ONCRPC_NAMELEN + 16], key[ONCRPC_NAMELEN + 16];
    char **nk;

    adapter_fn(g, t, key, sizeof(key));
    if (g->failed || have_key(g, key))
        return;
    nk = (char **)oncRPCRealloc(g->alloc, g->keys, (g->nkeys + 1) * sizeof(*nk));
    if (nk == NULL) {
        gerr(g, "out of memory");
        return;
    }
    g->keys = nk;
    g->keys[g->nkeys] = oncRPCStrdup(g->alloc, key);
    if (g->keys[g->nkeys] == NULL) {
        gerr(g, "out of memory");
        return;
    }
    g->nkeys++;

    if (t->kind == ONCRPC_XTYPE_STRING)
        sfmt(g, ptr, sizeof(ptr), "(char **)p");
    else
        sfmt(g, ptr, sizeof(ptr), "(%s *)p", ctype(t));
    P(g, "static int %s(oncRPCXdr *xdrs, void *p)\n{\n    return ", key);
    print_xdr_call(g, t, ptr);
    P(g, ";\n}\n\n");
}

static void emit_adapters(gen *g)
{
    const oncRPCXDef *d;
    size_t vi, i;
    char fn[ONCRPC_NAMELEN], an[ONCRPC_NAMELEN + 8];
    oncRPCXType at;

    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions; vi++) {
            const oncRPCXVersion *v = &d->versions[vi];
            for (i = 0; i < v->nprocs; i++) {
                const oncRPCXProc *pr = &v->procs[i];
                if (proc_fn(g, pr->name, v, fn, sizeof(fn)) != 0)
                    return;
                if (proc_argtype(pr, fn, &at, an, sizeof(an)))
                    emit_adapter(g, &at);
                if (pr->result.kind != ONCRPC_XTYPE_VOID)
                    emit_adapter(g, &pr->result);
            }
        }
    }
}

static void gen_client(gen *g)
{
    const oncRPCXDef *d;
    size_t vi, i, k;
    char fn[ONCRPC_NAMELEN], an[ONCRPC_NAMELEN + 8];
    char akey[ONCRPC_NAMELEN + 16], rkey[ONCRPC_NAMELEN + 16];
    oncRPCXType at;

    file_banner(g, "Client stubs");
    c_prelude(g);
    P(g, "#include <string.h>\n\n#include \"%s.h\"\n\n", g->base);
    emit_adapters(g);

    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions; vi++) {
            const oncRPCXVersion *v = &d->versions[vi];
            for (i = 0; i < v->nprocs; i++) {
                const oncRPCXProc *pr = &v->procs[i];
                int has_arg, has_res = pr->result.kind != ONCRPC_XTYPE_VOID;

                if (proc_fn(g, pr->name, v, fn, sizeof(fn)) != 0)
                    return;
                has_arg = proc_argtype(pr, fn, &at, an, sizeof(an));
                if (has_arg)
                    adapter_fn(g, &at, akey, sizeof(akey));
                if (has_res)
                    adapter_fn(g, &pr->result, rkey, sizeof(rkey));

                print_client_proto(g, pr, fn);
                P(g, "\n{\n");
                if (pr->nargs > 1) {
                    P(g, "    %sArgs args_;\n", fn);
                    for (k = 0; k < pr->nargs; k++) {
                        unsigned n = (unsigned)(k + 1);
                        if (pr->args[k].kind == ONCRPC_XTYPE_STRING)
                            P(g, "    args_.arg%u = (char *)arg%u;\n", n, n);
                        else
                            P(g, "    memcpy(&args_.arg%u, arg%u, sizeof(args_.arg%u));\n", n, n, n);
                    }
                } else if (pr->nargs == 1 && pr->args[0].kind == ONCRPC_XTYPE_STRING) {
                    P(g, "    char *arg_ = (char *)arg;   /* encoded before returning */\n");
                }
                if (has_res)
                    P(g, "    memset(res, 0, sizeof(*res));\n");
                P(g, "    return oncRPCClientCall(clnt, %s, ", pr->name);
                if (!has_arg)
                    P(g, "NULL, NULL, ");
                else if (pr->nargs > 1)
                    P(g, "%s, &args_, ", akey);
                else if (pr->args[0].kind == ONCRPC_XTYPE_STRING)
                    P(g, "%s, &arg_, ", akey);
                else
                    P(g, "%s, arg, ", akey);
                if (has_res)
                    P(g, "%s, res, cb, data);\n}\n\n", rkey);
                else
                    P(g, "NULL, NULL, cb, data);\n}\n\n");
            }
        }
    }
}

static void emit_dispatch_case(gen *g, const oncRPCXProc *pr, const char *fn)
{
    oncRPCXType at;
    char an[ONCRPC_NAMELEN + 8], akey[ONCRPC_NAMELEN + 16];
    int has_arg = proc_argtype(pr, fn, &at, an, sizeof(an));
    size_t k;

    P(g, "    case %s: {\n", pr->name);
    if (has_arg) {
        adapter_fn(g, &at, akey, sizeof(akey));
        if (at.kind == ONCRPC_XTYPE_STRING)
            P(g, "        char **arg = (char **)oncRPCSvcDecodeArgs(req, %s, "
                 "sizeof(char *));\n", akey);
        else
            P(g, "        %s *arg = (%s *)oncRPCSvcDecodeArgs(req, %s, sizeof(%s));\n",
              ctype(&at), ctype(&at), akey, ctype(&at));
        P(g, "        if (arg == NULL) {\n            oncRPCSvcErrDecode(req);\n"
             "            return;\n        }\n");
    }
    P(g, "        %sSvc(", fn);
    if (pr->nargs == 1) {
        if (pr->args[0].kind == ONCRPC_XTYPE_STRING)
            P(g, "*arg, ");
        else
            P(g, "(const %s *)arg, ", ctype(&pr->args[0]));
    } else {
        for (k = 0; k < pr->nargs; k++) {
            if (pr->args[k].kind == ONCRPC_XTYPE_STRING)
                P(g, "arg->arg%u, ", (unsigned)(k + 1));
            else
                P(g, "(const %s *)&arg->arg%u, ", ctype(&pr->args[k]), (unsigned)(k + 1));
        }
    }
    P(g, "req);\n        return;\n    }\n");
}

static void gen_server(gen *g)
{
    const oncRPCXDef *d;
    size_t vi, i;
    char fn[ONCRPC_NAMELEN], pfn[ONCRPC_NAMELEN], rkey[ONCRPC_NAMELEN + 16];

    file_banner(g, "Server dispatch, replies and registration");
    c_prelude(g);
    P(g, "#include <string.h>\n\n#include \"%s.h\"\n\n", g->base);
    emit_adapters(g);

    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions; vi++) {
            const oncRPCXVersion *v = &d->versions[vi];
            int has_null = 0;

            if (proc_fn(g, d->name, v, pfn, sizeof(pfn)) != 0)
                return;
            for (i = 0; i < v->nprocs; i++) {
                const oncRPCXProc *pr = &v->procs[i];
                if (proc_fn(g, pr->name, v, fn, sizeof(fn)) != 0)
                    return;
                print_reply_proto(g, pr, fn);
                P(g, "\n{\n");
                if (pr->result.kind == ONCRPC_XTYPE_VOID) {
                    P(g, "    return oncRPCSvcReply(req, NULL, NULL);\n}\n\n");
                } else if (pr->result.kind == ONCRPC_XTYPE_STRING) {
                    adapter_fn(g, &pr->result, rkey, sizeof(rkey));
                    P(g, "    char *res_ = (char *)res;   /* encoded, not retained */\n");
                    P(g, "    return oncRPCSvcReply(req, %s, &res_);\n}\n\n", rkey);
                } else {
                    adapter_fn(g, &pr->result, rkey, sizeof(rkey));
                    P(g, "    return oncRPCSvcReply(req, %s, res);\n}\n\n", rkey);
                }
            }
            for (i = 0; i < v->nprocs; i++) {
                int64_t num;
                if (oncRPCXSpecEval(g->spec, v->procs[i].number, &num) == 0 && num == 0)
                    has_null = 1;
            }
            P(g, "static void %sDispatch(oncRPCSvcReq *req)\n{\n", pfn);
            P(g, "    switch (req->proc) {\n");
            if (!has_null) {
                P(g, "    case 0:\n");
                P(g, "        (void)oncRPCSvcReply(req, NULL, NULL);\n");
                P(g, "        return;\n");
            }
            for (i = 0; i < v->nprocs; i++) {
                if (proc_fn(g, v->procs[i].name, v, fn, sizeof(fn)) != 0)
                    return;
                emit_dispatch_case(g, &v->procs[i], fn);
            }
            P(g, "    default:\n        oncRPCSvcErrNoProc(req);\n        return;\n    }\n}\n\n");
            P(g, "oncRPCStat %sRegister(oncRPCServer *srv, void *ctx)\n{\n", pfn);
            P(g, "    return oncRPCServerRegister(srv, %s, %s, %sDispatch, ctx);\n}\n\n",
              d->name, v->name, pfn);
        }
    }
}

/* ---- Skeletons --------------------------------------------------------------- */

static void skel_allocator(gen *g)
{
    P(g, "/* liboncrpc takes all memory from an application allocator. */\n");
    P(g, "static void *app_realloc(void *ctx, void *ptr, size_t size)\n{\n"
         "    (void)ctx;\n    return realloc(ptr, size);\n}\n\n");
    P(g, "static void app_free(void *ctx, void *ptr)\n{\n    (void)ctx;\n    free(ptr);\n}\n\n");
    P(g, "static const oncRPCAllocator app_alloc = { app_realloc, app_free, NULL };\n\n");
}

static void gen_server_skeleton(gen *g)
{
    const oncRPCXDef *d;
    size_t vi, i, k;
    char fn[ONCRPC_NAMELEN], pfn[ONCRPC_NAMELEN];

    file_banner(g, "Server implementation template");
    c_prelude(g);
    P(g, "#include <signal.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n");
    P(g, "#include \"%s.h\"\n\n", g->base);
    skel_allocator(g);

    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions; vi++) {
            const oncRPCXVersion *v = &d->versions[vi];
            for (i = 0; i < v->nprocs; i++) {
                const oncRPCXProc *pr = &v->procs[i];
                if (proc_fn(g, pr->name, v, fn, sizeof(fn)) != 0)
                    return;
                print_svc_proto(g, pr, fn);
                P(g, "\n{\n");
                if (pr->result.kind != ONCRPC_XTYPE_VOID && pr->result.kind != ONCRPC_XTYPE_STRING) {
                    P(g, "    ");
                    print_var(g, &pr->result, "res");
                    P(g, ";\n\n    memset(&res, 0, sizeof(res));\n");
                }
                if (pr->nargs == 1)
                    P(g, "    (void)arg;\n");
                for (k = 0; pr->nargs > 1 && k < pr->nargs; k++)
                    P(g, "    (void)arg%u;\n", (unsigned)(k + 1));
                P(g, "    /* TODO: implement %s. To answer later, keep req and call\n"
                     "     * %sReply() when the result is ready. */\n", pr->name, fn);
                if (pr->result.kind == ONCRPC_XTYPE_VOID)
                    P(g, "    (void)%sReply(req);\n}\n\n", fn);
                else if (pr->result.kind == ONCRPC_XTYPE_STRING)
                    P(g, "    (void)%sReply(req, \"\");\n}\n\n", fn);
                else
                    P(g, "    (void)%sReply(req, (const %s *)&res);\n}\n\n", fn, ctype(&pr->result));
            }
        }
    }

    P(g, "static oncRPCServer *g_server;\nstatic uv_signal_t g_sigint;\n\n");
    P(g, "static void on_pmap(oncRPCServer *srv, oncRPCStat st, void *data)\n{\n"
         "    (void)srv;\n"
         "    if (st != ONCRPC_OK)\n"
         "        fprintf(stderr, \"port mapper registration of %%s failed: %%s\\n\",\n"
         "                (const char *)data, oncRPCStrError(st));\n}\n\n");
    P(g, "static void on_sigint(uv_signal_t *h, int signum)\n{\n"
         "    (void)signum;\n"
         "    uv_close((uv_handle_t *)h, NULL);\n"
         "    /* Registered with the port mapper? Use oncRPCServerPmapUnregister first. */\n"
         "    oncRPCServerClose(g_server, NULL, NULL);\n}\n\n");
    P(g, "int main(int argc, char **argv)\n{\n");
    P(g, "    uv_loop_t *loop = uv_default_loop();\n");
    P(g, "    uint16_t port = (argc > 1) ? (uint16_t)atoi(argv[1]) : 0;\n\n");
    P(g, "    if (oncRPCServerCreate(loop, &app_alloc, &g_server) != ONCRPC_OK ||\n");
    P(g, "        oncRPCServerListen(g_server, ONCRPC_PROTO_TCP, NULL, port) != ONCRPC_OK ||\n");
    P(g, "        oncRPCServerListen(g_server, ONCRPC_PROTO_UDP, NULL, port) != ONCRPC_OK) {\n");
    P(g, "        fprintf(stderr, \"cannot create server\\n\");\n        return 1;\n    }\n");
    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions; vi++) {
            if (proc_fn(g, d->name, &d->versions[vi], pfn, sizeof(pfn)) != 0)
                return;
            P(g, "    (void)%sRegister(g_server, NULL);\n", pfn);
            P(g, "    if (port == 0)\n");
            P(g, "        (void)oncRPCServerPmapRegister(g_server, %s, %s, on_pmap, \"%s\");\n",
              d->name, d->versions[vi].name, d->versions[vi].name);
        }
    }
    P(g, "    printf(\"listening on tcp/%%u udp/%%u\\n\",\n");
    P(g, "           (unsigned)oncRPCServerPort(g_server, ONCRPC_PROTO_TCP),\n");
    P(g, "           (unsigned)oncRPCServerPort(g_server, ONCRPC_PROTO_UDP));\n");
    P(g, "    uv_signal_init(loop, &g_sigint);\n");
    P(g, "    uv_signal_start(&g_sigint, on_sigint, SIGINT);\n");
    P(g, "    uv_run(loop, UV_RUN_DEFAULT);\n");
    P(g, "    (void)uv_loop_close(loop);\n    return 0;\n}\n");
}

static void gen_client_skeleton(gen *g)
{
    const oncRPCXDef *d;
    size_t vi, i, k;
    char fn[ONCRPC_NAMELEN], pfn[ONCRPC_NAMELEN], an[ONCRPC_NAMELEN + 8];
    oncRPCXType at;

    file_banner(g, "Example client");
    c_prelude(g);
    P(g, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n");
    P(g, "#include \"%s.h\"\n\n", g->base);
    skel_allocator(g);
    P(g, "static void on_closed(void *data)\n{\n    (void)data;\n}\n\n");
    P(g, "/* Each client counts its outstanding calls; it closes after the last. */\n");
    P(g, "static void call_done(oncRPCClient *clnt)\n{\n"
         "    int *pending = (int *)oncRPCClientData(clnt);\n"
         "    if (--*pending == 0)\n"
         "        oncRPCClientClose(clnt, on_closed, NULL);\n}\n\n");

    /* Result storage and a callback per procedure. */
    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions; vi++) {
            const oncRPCXVersion *v = &d->versions[vi];
            for (i = 0; i < v->nprocs; i++) {
                const oncRPCXProc *pr = &v->procs[i];
                char rv[ONCRPC_NAMELEN + 8];
                if (proc_fn(g, pr->name, v, fn, sizeof(fn)) != 0)
                    return;
                if (pr->result.kind != ONCRPC_XTYPE_VOID) {
                    sfmt(g, rv, sizeof(rv), "res%s", fn + 6);
                    P(g, "static ");
                    print_var(g, &pr->result, rv);
                    P(g, ";\n");
                }
                P(g, "static void on%s(oncRPCClient *clnt, oncRPCStat st, void *res, void *data)\n{\n",
                  fn + 6);
                P(g, "    (void)data;\n");
                P(g, "    printf(\"%s: %%s\\n\", oncRPCStrError(st));\n", pr->name);
                if (pr->result.kind == ONCRPC_XTYPE_NAMED) {
                    char ff[ONCRPC_NAMELEN + 16];
                    free_fn(g, ff, sizeof(ff), pr->result.name);
                    P(g, "    if (st == ONCRPC_OK)\n        %s(oncRPCClientAllocator(clnt), "
                         "(%s *)res);\n", ff, pr->result.name);
                }
                else if (pr->result.kind == ONCRPC_XTYPE_STRING)
                    P(g, "    if (st == ONCRPC_OK)\n        oncRPCFree(oncRPCClientAllocator(clnt), "
                         "*(char **)res);\n");
                else
                    P(g, "    (void)res;\n");
                P(g, "    call_done(clnt);\n}\n\n");
            }
        }
    }

    P(g, "int main(int argc, char **argv)\n{\n");
    P(g, "    uv_loop_t *loop = uv_default_loop();\n");
    P(g, "    const char *host = (argc > 1) ? argv[1] : \"localhost\";\n");
    P(g, "    uint16_t port = (argc > 2) ? (uint16_t)atoi(argv[2]) : 0;\n");
    P(g, "    oncRPCClient *clnt;\n    oncRPCStat st;\n");
    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions; vi++) {
            if (proc_fn(g, d->name, &d->versions[vi], pfn, sizeof(pfn)) != 0)
                return;
            P(g, "    static int pending%s;\n", pfn + 6);
        }
    }
    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions; vi++) {
            const oncRPCXVersion *v = &d->versions[vi];
            if (proc_fn(g, d->name, v, pfn, sizeof(pfn)) != 0)
                return;
            P(g, "\n    /* %s version %s */\n", d->name, v->name);
            P(g, "    st = oncRPCClientCreate(loop, &app_alloc, host, port, %s, %s, ONCRPC_PROTO_TCP,\n"
                 "                            NULL, &pending%s, &clnt);\n",
              d->name, v->name, pfn + 6);
            P(g, "    if (st != ONCRPC_OK) {\n");
            P(g, "        fprintf(stderr, \"%%s: %%s\\n\", host, oncRPCStrError(st));\n");
            P(g, "        return 1;\n    }\n");
            for (i = 0; i < v->nprocs; i++) {
                const oncRPCXProc *pr = &v->procs[i];
                if (proc_fn(g, pr->name, v, fn, sizeof(fn)) != 0)
                    return;
                (void)proc_argtype(pr, fn, &at, an, sizeof(an));
                P(g, "    {\n");
                for (k = 0; k < pr->nargs; k++) {
                    char nm[16];
                    sfmt(g, nm, sizeof(nm), "arg%u", (unsigned)(k + 1));
                    if (pr->args[k].kind == ONCRPC_XTYPE_STRING) {
                        P(g, "        const char *%s = \"\";\n", nm);
                    } else {
                        P(g, "        ");
                        print_var(g, &pr->args[k], nm);
                        P(g, ";\n");
                    }
                }
                for (k = 0; k < pr->nargs; k++)
                    if (pr->args[k].kind != ONCRPC_XTYPE_STRING)
                        P(g, "        memset(&arg%u, 0, sizeof(arg%u));\n",
                          (unsigned)(k + 1), (unsigned)(k + 1));
                P(g, "        if (%s(clnt", fn);
                for (k = 0; k < pr->nargs; k++) {
                    if (pr->args[k].kind == ONCRPC_XTYPE_STRING)
                        P(g, ", arg%u", (unsigned)(k + 1));
                    else
                        P(g, ", (const %s *)&arg%u", ctype(&pr->args[k]), (unsigned)(k + 1));
                }
                if (pr->result.kind != ONCRPC_XTYPE_VOID)
                    P(g, ", &res%s", fn + 6);
                P(g, ", on%s, NULL) == ONCRPC_OK)\n", fn + 6);
                P(g, "            pending%s++;\n    }\n", pfn + 6);
            }
            P(g, "    if (pending%s == 0)\n        oncRPCClientClose(clnt, on_closed, NULL);\n",
              pfn + 6);
        }
    }
    P(g, "\n    uv_run(loop, UV_RUN_DEFAULT);\n    (void)uv_loop_close(loop);\n    return 0;\n}\n");
}

/* ---- Name checks ---------------------------------------------------------------- */

/* Names liboncrpc's headers use in the oncRPCXdr namespace, which generated
 * XDR routines share. Keep in step with xdr.h, oncrpc.h and xspec.h. */
static const char *const reserved_names[] = {
    "oncRPCXdr", "oncRPCXdrArray", "oncRPCXdrArrayAlloc", "oncRPCXdrArrayLen",
    "oncRPCXdrAuthSys", "oncRPCXdrBool", "oncRPCXdrBytes", "oncRPCXdrData",
    "oncRPCXdrDestroy", "oncRPCXdrDetach", "oncRPCXdrDouble", "oncRPCXdrEnum",
    "oncRPCXdrFloat", "oncRPCXdrFree", "oncRPCXdrGetPos", "oncRPCXdrInitDecode",
    "oncRPCXdrInitEncode", "oncRPCXdrInitEncodeDynamic", "oncRPCXdrInitFree",
    "oncRPCXdrInt16", "oncRPCXdrInt32", "oncRPCXdrInt64", "oncRPCXdrInt8",
    "oncRPCXdrLength", "oncRPCXdrMemFree", "oncRPCXdrOp", "oncRPCXdrOpaque",
    "oncRPCXdrPointer", "oncRPCXdrProc", "oncRPCXdrRemaining", "oncRPCXdrReset",
    "oncRPCXdrSetPos", "oncRPCXdrString", "oncRPCXdrUint16", "oncRPCXdrUint32",
    "oncRPCXdrUint64", "oncRPCXdrUint8", "oncRPCXdrVector", "oncRPCXdrVoid"
};

typedef struct namelist {
    char  **names;
    char  **from;    /* what each name was made from, for messages */
    size_t  n;
} namelist;

static void add_name(gen *g, namelist *l, const char *name, const char *from)
{
    size_t i;
    char **nn, **nf;

    if (g->failed)
        return;
    for (i = 0; i < sizeof(reserved_names) / sizeof(reserved_names[0]); i++)
        if (strcmp(name, reserved_names[i]) == 0) {
            gerr(g, "%s would generate %s, which liboncrpc already uses; please rename it",
                 from, name);
            return;
        }
    for (i = 0; i < l->n; i++)
        if (strcmp(l->names[i], name) == 0) {
            gerr(g, "%s and %s both generate %s; please rename one of them", l->from[i], from,
                 name);
            return;
        }
    nn = (char **)oncRPCRealloc(g->alloc, l->names, (l->n + 1) * sizeof(*nn));
    if (nn == NULL) {
        gerr(g, "out of memory");
        return;
    }
    l->names = nn;
    nf = (char **)oncRPCRealloc(g->alloc, l->from, (l->n + 1) * sizeof(*nf));
    if (nf == NULL) {
        gerr(g, "out of memory");
        return;
    }
    l->from = nf;
    l->names[l->n] = oncRPCStrdup(g->alloc, name);
    l->from[l->n] = oncRPCStrdup(g->alloc, from);
    l->n++;
    if (l->names[l->n - 1] == NULL || l->from[l->n - 1] == NULL)
        gerr(g, "out of memory");
}

static void add_type_names(gen *g, namelist *l, const char *type, const char *from)
{
    char buf[ONCRPC_NAMELEN + 16];
    xdr_fn(g, buf, sizeof(buf), type);
    add_name(g, l, buf, from);
    free_fn(g, buf, sizeof(buf), type);
    add_name(g, l, buf, from);
}

/*
 * Work out every name the generated code will define and check that none
 * is used twice or clashes with the library, so that a clash is reported
 * here, with the .x names involved, rather than by the C compiler.
 */
static void check_names(gen *g)
{
    static const char *const proc_roles[] = { "", "Svc", "Reply" };
    const oncRPCXDef *d;
    namelist l;
    char buf[ONCRPC_NAMELEN + 16], fn[ONCRPC_NAMELEN], from[ONCRPC_NAMELEN + 64];
    size_t vi, i, r;

    memset(&l, 0, sizeof(l));
    for (d = g->spec->defs; d != NULL && !g->failed; d = d->next) {
        if (is_type_def(d)) {
            if (strncmp(d->name, "oncRPC", 6) == 0) {
                gerr(g, "type '%s': names beginning with oncRPC are reserved for liboncrpc",
                     d->name);
                break;
            }
            sfmt(g, from, sizeof(from), "type '%s'", d->name);
            add_type_names(g, &l, d->name, from);
            continue;
        }
        if (d->kind != ONCRPC_XDEF_PROGRAM)
            continue;
        for (vi = 0; vi < d->nversions && !g->failed; vi++) {
            const oncRPCXVersion *v = &d->versions[vi];
            if (proc_fn(g, d->name, v, fn, sizeof(fn)) != 0)
                break;
            sfmt(g, from, sizeof(from), "program '%s' version '%s'", d->name, v->name);
            sfmt(g, buf, sizeof(buf), "%sRegister", fn);
            add_name(g, &l, buf, from);
            for (i = 0; i < v->nprocs && !g->failed; i++) {
                const oncRPCXProc *pr = &v->procs[i];
                if (proc_fn(g, pr->name, v, fn, sizeof(fn)) != 0)
                    break;
                sfmt(g, from, sizeof(from), "procedure '%s' of version '%s'", pr->name, v->name);
                for (r = 0; r < sizeof(proc_roles) / sizeof(proc_roles[0]); r++) {
                    sfmt(g, buf, sizeof(buf), "%s%s", fn, proc_roles[r]);
                    add_name(g, &l, buf, from);
                }
                if (pr->nargs > 1) {
                    sfmt(g, buf, sizeof(buf), "%sArgs", fn);
                    add_name(g, &l, buf, from);
                    add_type_names(g, &l, buf, from);
                }
            }
        }
    }
    for (i = 0; i < l.n; i++) {
        oncRPCFree(g->alloc, l.names[i]);
        oncRPCFree(g->alloc, l.from[i]);
    }
    oncRPCFree(g->alloc, l.names);
    oncRPCFree(g->alloc, l.from);
}

int oncRPCXSpecGenerate(const oncRPCXSpec *spec, oncRPCXGenKind kind, const char *basename,
                        char **out, size_t *outlen, char *err, size_t errlen)
{
    gen g;
    size_t i;

    if (err != NULL && errlen > 0)
        err[0] = '\0';
    if (out != NULL)
        *out = NULL;
    if (outlen != NULL)
        *outlen = 0;
    if (spec == NULL || basename == NULL || out == NULL)
        return -1;
    memset(&g, 0, sizeof(g));
    g.alloc = spec->alloc;
    g.spec = spec;
    g.base = basename;
    g.err = err;
    g.errlen = errlen;
    g.outcap = 16384;
    g.out = (char *)oncRPCRealloc(g.alloc, NULL, g.outcap);
    if (g.out == NULL) {
        gerr(&g, "out of memory");
        return -1;
    }
    g.out[0] = '\0';

    check_names(&g);
    if (!g.failed) {
        switch (kind) {
        case ONCRPC_GEN_HEADER:          gen_header(&g); break;
        case ONCRPC_GEN_XDR:             gen_xdr(&g); break;
        case ONCRPC_GEN_CLIENT:          gen_client(&g); break;
        case ONCRPC_GEN_SERVER:          gen_server(&g); break;
        case ONCRPC_GEN_SERVER_SKELETON: gen_server_skeleton(&g); break;
        case ONCRPC_GEN_CLIENT_SKELETON: gen_client_skeleton(&g); break;
        default:                         gerr(&g, "unknown output kind"); break;
        }
    }
    for (i = 0; i < g.nkeys; i++)
        oncRPCFree(g.alloc, g.keys[i]);
    oncRPCFree(g.alloc, g.keys);
    if (g.failed) {
        if (err != NULL && errlen > 0 && err[0] == '\0')
            snprintf(err, errlen, "out of memory");
        oncRPCFree(g.alloc, g.out);
        return -1;
    }
    *out = g.out;
    if (outlen != NULL)
        *outlen = g.outlen;
    return 0;
}
