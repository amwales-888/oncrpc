/*
 * xparse.c - recursive descent parser for .x specifications.
 *
 * All memory belonging to a parsed specification lives in an arena built
 * on the application's allocator and released by oncRPCXSpecFree().
 * Errors unwind with longjmp.
 */
#include "xspec.h"

#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>   /* strtoll only */
#include <string.h>

/* ---- Arena ------------------------------------------------------------- */

typedef union align_u {
    long double ld;
    long long   ll;
    void       *p;
    double      d;
} align_u;

typedef struct anode {
    struct anode *next;
} anode;

typedef struct arena {
    anode                 *head;
    const oncRPCAllocator *alloc;
} arena;

#define ONCRPC_ARENA_HDR (sizeof(align_u) > sizeof(anode) ? sizeof(align_u) : sizeof(anode))

static void *arena_alloc(arena *a, size_t n)
{
    unsigned char *m;
    if (n > ((size_t)-1) - ONCRPC_ARENA_HDR - 1)
        return NULL;
    m = (unsigned char *)oncRPCAlloc(a->alloc, ONCRPC_ARENA_HDR + (n ? n : 1));
    if (m == NULL)
        return NULL;
    ((anode *)m)->next = a->head;
    a->head = (anode *)m;
    return m + ONCRPC_ARENA_HDR;
}

static void arena_release(arena *a)
{
    while (a->head != NULL) {
        anode *n = a->head;
        a->head = n->next;
        oncRPCFree(a->alloc, n);
    }
}

/* ---- Parser state ------------------------------------------------------ */

typedef enum tkind { TK_EOF, TK_IDENT, TK_NUMBER, TK_PUNCT, TK_PASSTHRU } tkind;

typedef struct pname {
    const char   *name;
    const char   *proc_number;   /* set for procedure names */
    struct pname *next;
} pname;

typedef struct parser {
    const char   *src;
    size_t        pos;
    int           line;
    int           bol;
    tkind         tk;
    const char   *text;
    char          punct;
    int           tline;
    arena        *ar;
    oncRPCXSpec *spec;
    oncRPCXDef  *tail;
    pname        *names;
    jmp_buf       jb;
    char         *err;
    size_t        errlen;
    const char   *fname;
    int           anon;
} parser;

static const char *const reserved_words[] = {
    "bool", "case", "const", "default", "double", "quadruple", "enum", "float",
    "hyper", "int", "opaque", "program", "string", "struct", "switch", "typedef",
    "union", "unsigned", "version", "void", "char", "short", "long", NULL
};

static int is_reserved(const char *s)
{
    int i;
    for (i = 0; reserved_words[i] != NULL; i++)
        if (strcmp(s, reserved_words[i]) == 0)
            return 1;
    return 0;
}

static void perr(parser *p, const char *fmt, ...)
{
    if (p->err != NULL && p->errlen > 0) {
        va_list ap;
        int n = snprintf(p->err, p->errlen, "%s:%d: error: ", p->fname, p->tline);
        if (n >= 0 && (size_t)n < p->errlen) {
            va_start(ap, fmt);
            vsnprintf(p->err + n, p->errlen - (size_t)n, fmt, ap);
            va_end(ap);
        }
    }
    longjmp(p->jb, 1);
}

static void *palloc(parser *p, size_t n)
{
    void *m = arena_alloc(p->ar, n);
    if (m == NULL)
        perr(p, "out of memory");
    return m;
}

static char *pstrndup(parser *p, const char *s, size_t n)
{
    char *d = (char *)palloc(p, n + 1);
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static char *pstrcat3(parser *p, const char *a, const char *b, const char *c)
{
    size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
    char *d = (char *)palloc(p, la + lb + lc + 1);
    memcpy(d, a, la);
    memcpy(d + la, b, lb);
    memcpy(d + la + lb, c, lc + 1);
    return d;
}

/* Grow an arena array by one element; returns the new array. */
static void *pgrow(parser *p, void *old, size_t count, size_t elsize)
{
    void *n = palloc(p, (count + 1) * elsize);
    if (count > 0)
        memcpy(n, old, count * elsize);
    return n;
}

/* ---- Lexer ------------------------------------------------------------- */

static void lex(parser *p)
{
    const char *s = p->src;

    for (;;) {
        char c = s[p->pos];
        size_t st;

        if (c == '\0') {
            p->tk = TK_EOF;
            p->text = "end of file";
            p->tline = p->line;
            return;
        }
        if (c == '\n') {
            p->line++;
            p->pos++;
            p->bol = 1;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            p->pos++;
            continue;
        }
        if (c == '/' && s[p->pos + 1] == '*') {
            size_t i = p->pos + 2;
            p->tline = p->line;
            for (;;) {
                if (s[i] == '\0')
                    perr(p, "unterminated comment");
                if (s[i] == '\n')
                    p->line++;
                if (s[i] == '*' && s[i + 1] == '/') {
                    i += 2;
                    break;
                }
                i++;
            }
            p->pos = i;
            continue;
        }
        if (c == '/' && s[p->pos + 1] == '/') {
            while (s[p->pos] != '\0' && s[p->pos] != '\n')
                p->pos++;
            continue;
        }
        if (p->bol && c == '%') {
            size_t e;
            st = ++p->pos;
            while (s[p->pos] != '\0' && s[p->pos] != '\n')
                p->pos++;
            e = p->pos;
            if (e > st && s[e - 1] == '\r')
                e--;
            p->tk = TK_PASSTHRU;
            p->text = pstrndup(p, s + st, e - st);
            p->tline = p->line;
            return;
        }
        if (p->bol && c == '#') {
            while (s[p->pos] != '\0' && s[p->pos] != '\n')
                p->pos++;
            continue;
        }

        p->bol = 0;
        p->tline = p->line;
        st = p->pos;
        if (isalpha((unsigned char)c) || c == '_') {
            while (isalnum((unsigned char)s[p->pos]) || s[p->pos] == '_')
                p->pos++;
            p->tk = TK_IDENT;
            p->text = pstrndup(p, s + st, p->pos - st);
            return;
        }
        if (isdigit((unsigned char)c) || (c == '-' && isdigit((unsigned char)s[p->pos + 1]))) {
            if (c == '-')
                p->pos++;
            if (s[p->pos] == '0' && (s[p->pos + 1] == 'x' || s[p->pos + 1] == 'X')) {
                p->pos += 2;
                if (!isxdigit((unsigned char)s[p->pos]))
                    perr(p, "malformed hexadecimal constant");
                while (isxdigit((unsigned char)s[p->pos]))
                    p->pos++;
            } else {
                while (isdigit((unsigned char)s[p->pos]))
                    p->pos++;
            }
            if (isalnum((unsigned char)s[p->pos]) || s[p->pos] == '_')
                perr(p, "malformed number");
            p->tk = TK_NUMBER;
            p->text = pstrndup(p, s + st, p->pos - st);
            return;
        }
        if (strchr("{}()[]<>;,=:*", c) != NULL) {
            p->tk = TK_PUNCT;
            p->punct = c;
            p->text = pstrndup(p, s + st, 1);
            p->pos++;
            return;
        }
        if (isprint((unsigned char)c))
            perr(p, "unexpected character '%c'", c);
        perr(p, "unexpected character (code %d)", (int)(unsigned char)c);
    }
}

/* ---- Token helpers ------------------------------------------------------ */

static int is_punct(parser *p, char c)
{
    return p->tk == TK_PUNCT && p->punct == c;
}

static int is_kw(parser *p, const char *kw)
{
    return p->tk == TK_IDENT && strcmp(p->text, kw) == 0;
}

static void expected(parser *p, const char *what)
{
    if (p->tk == TK_EOF)
        perr(p, "expected %s before end of file", what);
    perr(p, "expected %s before '%s'", what, p->text);
}

static void expect_punct(parser *p, char c)
{
    char what[8];
    if (!is_punct(p, c)) {
        snprintf(what, sizeof(what), "'%c'", c);
        expected(p, what);
    }
    lex(p);
}

static void expect_kw(parser *p, const char *kw)
{
    if (!is_kw(p, kw)) {
        char what[32];
        snprintf(what, sizeof(what), "'%s'", kw);
        expected(p, what);
    }
    lex(p);
}

static const char *expect_ident(parser *p)
{
    const char *s;
    if (p->tk != TK_IDENT)
        expected(p, "identifier");
    if (is_reserved(p->text))
        perr(p, "'%s' is a reserved word", p->text);
    s = p->text;
    lex(p);
    return s;
}

static const char *parse_value(parser *p)
{
    const char *s;
    if (p->tk == TK_NUMBER || (p->tk == TK_IDENT && !is_reserved(p->text))) {
        s = p->text;
        lex(p);
        return s;
    }
    expected(p, "constant or identifier");
    return NULL;
}

/* ---- Names -------------------------------------------------------------- */

static pname *find_name(parser *p, const char *name)
{
    pname *n;
    for (n = p->names; n != NULL; n = n->next)
        if (strcmp(n->name, name) == 0)
            return n;
    return NULL;
}

static void declare_name(parser *p, const char *name, int line)
{
    pname *n;
    if (find_name(p, name) != NULL) {
        p->tline = line;
        perr(p, "'%s' is already defined", name);
    }
    n = (pname *)palloc(p, sizeof(*n));
    n->name = name;
    n->next = p->names;
    p->names = n;
}

/* The same procedure may appear in several versions with the same number. */
static void declare_proc(parser *p, const char *name, const char *number, int line)
{
    pname *n = find_name(p, name);
    if (n != NULL) {
        if (n->proc_number != NULL && strcmp(n->proc_number, number) == 0)
            return;
        p->tline = line;
        perr(p, "'%s' is already defined", name);
    }
    n = (pname *)palloc(p, sizeof(*n));
    n->name = name;
    n->proc_number = number;
    n->next = p->names;
    p->names = n;
}

static oncRPCXDef *new_def(parser *p, oncRPCXDefKind k, const char *name, int line)
{
    oncRPCXDef *d = (oncRPCXDef *)palloc(p, sizeof(*d));
    d->kind = k;
    d->name = name;
    d->line = line;
    return d;
}

static void append_def(parser *p, oncRPCXDef *d)
{
    if (p->tail != NULL)
        p->tail->next = d;
    else
        p->spec->defs = d;
    p->tail = d;
}

/* ---- Grammar ------------------------------------------------------------ */

static void parse_decl(parser *p, oncRPCXDecl *d);
static void parse_enum_body(parser *p, oncRPCXDef *def);
static void parse_struct_body(parser *p, oncRPCXDef *def);
static void parse_union_body(parser *p, oncRPCXDef *def);

/* Give an anonymous inline type defined in declaration d a real name. */
static void name_inline(parser *p, oncRPCXDecl *d, const char *name)
{
    oncRPCXDef *idef = d->type.inlineDef;
    if (idef == NULL || !idef->anonymous)
        return;
    idef->name = name;
    idef->anonymous = 0;
    d->type.name = name;
    declare_name(p, name, idef->line);
}

static void name_inline_field(parser *p, oncRPCXDecl *d, const char *parent)
{
    if (d->type.inlineDef != NULL && d->type.inlineDef->anonymous)
        name_inline(p, d, pstrcat3(p, parent, "_", d->name ? d->name : "arm"));
}

static oncRPCXType parse_type_spec(parser *p, int allow_inline)
{
    oncRPCXType t;
    memset(&t, 0, sizeof(t));

    if (is_kw(p, "unsigned")) {
        lex(p);
        t.kind = ONCRPC_XTYPE_UINT;
        if (is_kw(p, "int") || is_kw(p, "long")) {
            lex(p);
        } else if (is_kw(p, "hyper")) {
            t.kind = ONCRPC_XTYPE_UHYPER;
            lex(p);
        } else if (is_kw(p, "char")) {
            t.kind = ONCRPC_XTYPE_UCHAR;
            lex(p);
        } else if (is_kw(p, "short")) {
            t.kind = ONCRPC_XTYPE_USHORT;
            lex(p);
        }
        return t;
    }
    if (p->tk == TK_IDENT) {
        static const struct { const char *kw; oncRPCXTypeKind k; } simple[] = {
            { "int", ONCRPC_XTYPE_INT },       { "long", ONCRPC_XTYPE_INT },
            { "hyper", ONCRPC_XTYPE_HYPER },   { "float", ONCRPC_XTYPE_FLOAT },
            { "double", ONCRPC_XTYPE_DOUBLE }, { "bool", ONCRPC_XTYPE_BOOL },
            { "char", ONCRPC_XTYPE_CHAR },     { "short", ONCRPC_XTYPE_SHORT },
            { NULL, ONCRPC_XTYPE_VOID }
        };
        int i;
        for (i = 0; simple[i].kw != NULL; i++) {
            if (strcmp(p->text, simple[i].kw) == 0) {
                t.kind = simple[i].k;
                lex(p);
                return t;
            }
        }
        if (is_kw(p, "quadruple"))
            perr(p, "'quadruple' is not supported (no portable C type)");
    }
    if (is_kw(p, "struct") || is_kw(p, "union") || is_kw(p, "enum")) {
        oncRPCXDefKind k = is_kw(p, "struct") ? ONCRPC_XDEF_STRUCT
                           : is_kw(p, "union") ? ONCRPC_XDEF_UNION : ONCRPC_XDEF_ENUM;
        const char *name = NULL;
        int line = p->tline, has_body;
        oncRPCXDef *def;

        lex(p);
        if (p->tk == TK_IDENT && !is_reserved(p->text)) {
            name = p->text;
            lex(p);
        }
        has_body = (k == ONCRPC_XDEF_UNION) ? is_kw(p, "switch") : is_punct(p, '{');
        if (!has_body) {
            if (name == NULL)
                expected(p, k == ONCRPC_XDEF_UNION ? "'switch' or union name"
                                                   : "'{' or type name");
            t.kind = ONCRPC_XTYPE_NAMED;
            t.name = name;
            return t;
        }
        if (!allow_inline)
            perr(p, "type definitions are not allowed here");
        if (name != NULL) {
            declare_name(p, name, line);
            def = new_def(p, k, name, line);
        } else {
            char tmp[48];
            snprintf(tmp, sizeof(tmp), "oncRPCAnon%d", ++p->anon);
            def = new_def(p, k, pstrndup(p, tmp, strlen(tmp)), line);
            def->anonymous = 1;
        }
        if (k == ONCRPC_XDEF_STRUCT)
            parse_struct_body(p, def);
        else if (k == ONCRPC_XDEF_UNION)
            parse_union_body(p, def);
        else
            parse_enum_body(p, def);
        append_def(p, def);
        t.kind = ONCRPC_XTYPE_NAMED;
        t.name = def->name;
        t.inlineDef = def;
        return t;
    }
    if (p->tk == TK_IDENT && !is_reserved(p->text)) {
        t.kind = ONCRPC_XTYPE_NAMED;
        t.name = p->text;
        lex(p);
        return t;
    }
    expected(p, "type");
    return t;
}

static void parse_bound(parser *p, oncRPCXDecl *d, int allow_fixed)
{
    if (allow_fixed && is_punct(p, '[')) {
        lex(p);
        d->size = parse_value(p);
        expect_punct(p, ']');
        d->kind = (d->kind == ONCRPC_XDECL_OPAQUE_VAR) ? ONCRPC_XDECL_OPAQUE_FIXED
                                                       : ONCRPC_XDECL_FIXED_ARRAY;
        return;
    }
    if (is_punct(p, '<')) {
        lex(p);
        if (!is_punct(p, '>'))
            d->size = parse_value(p);
        expect_punct(p, '>');
        return;
    }
    expected(p, allow_fixed ? "'[' or '<'" : "'<'");
}

static void parse_decl(parser *p, oncRPCXDecl *d)
{
    memset(d, 0, sizeof(*d));
    d->line = p->tline;

    if (is_kw(p, "void")) {
        lex(p);
        d->kind = ONCRPC_XDECL_VOID;
        return;
    }
    if (is_kw(p, "opaque")) {
        lex(p);
        d->name = expect_ident(p);
        d->kind = ONCRPC_XDECL_OPAQUE_VAR;
        parse_bound(p, d, 1);
        return;
    }
    if (is_kw(p, "string")) {
        lex(p);
        d->name = expect_ident(p);
        d->kind = ONCRPC_XDECL_STRING;
        parse_bound(p, d, 0);
        return;
    }
    d->type = parse_type_spec(p, 1);
    if (is_punct(p, '*')) {
        lex(p);
        d->name = expect_ident(p);
        d->kind = ONCRPC_XDECL_OPTIONAL;
        return;
    }
    d->name = expect_ident(p);
    if (is_punct(p, '[') || is_punct(p, '<')) {
        d->kind = ONCRPC_XDECL_VAR_ARRAY;
        parse_bound(p, d, 1);
    } else {
        d->kind = ONCRPC_XDECL_PLAIN;
    }
}

static void parse_enum_body(parser *p, oncRPCXDef *def)
{
    expect_punct(p, '{');
    for (;;) {
        oncRPCXEnumItem it;
        int line = p->tline;
        it.name = expect_ident(p);
        it.value = NULL;
        declare_name(p, it.name, line);
        if (is_punct(p, '=')) {
            lex(p);
            it.value = parse_value(p);
        }
        def->items = (oncRPCXEnumItem *)pgrow(p, def->items, def->nitems, sizeof(it));
        def->items[def->nitems++] = it;
        if (is_punct(p, ',')) {
            lex(p);
            if (is_punct(p, '}'))
                break;
            continue;
        }
        break;
    }
    expect_punct(p, '}');
}

static void check_dup_field(parser *p, const oncRPCXDecl *list, size_t n,
                            const oncRPCXDecl *d)
{
    size_t i;
    if (d->name == NULL)
        return;
    for (i = 0; i < n; i++) {
        if (list[i].name != NULL && strcmp(list[i].name, d->name) == 0) {
            p->tline = d->line;
            perr(p, "duplicate member '%s'", d->name);
        }
    }
}

static void parse_struct_body(parser *p, oncRPCXDef *def)
{
    expect_punct(p, '{');
    do {
        oncRPCXDecl d;
        parse_decl(p, &d);
        if (d.kind == ONCRPC_XDECL_VOID) {
            p->tline = d.line;
            perr(p, "'void' is not allowed as a struct member");
        }
        name_inline_field(p, &d, def->name);
        check_dup_field(p, def->fields, def->nfields, &d);
        def->fields = (oncRPCXDecl *)pgrow(p, def->fields, def->nfields, sizeof(d));
        def->fields[def->nfields++] = d;
        expect_punct(p, ';');
    } while (!is_punct(p, '}'));
    lex(p);
}

static void parse_union_body(parser *p, oncRPCXDef *def)
{
    oncRPCXDecl *arms = NULL;
    size_t narms = 0;

    expect_kw(p, "switch");
    expect_punct(p, '(');
    parse_decl(p, &def->disc);
    if (def->disc.kind != ONCRPC_XDECL_PLAIN || def->disc.type.inlineDef != NULL ||
        !(def->disc.type.kind == ONCRPC_XTYPE_INT || def->disc.type.kind == ONCRPC_XTYPE_UINT ||
          def->disc.type.kind == ONCRPC_XTYPE_BOOL || def->disc.type.kind == ONCRPC_XTYPE_NAMED ||
          def->disc.type.kind == ONCRPC_XTYPE_CHAR || def->disc.type.kind == ONCRPC_XTYPE_UCHAR ||
          def->disc.type.kind == ONCRPC_XTYPE_SHORT ||
          def->disc.type.kind == ONCRPC_XTYPE_USHORT)) {
        p->tline = def->disc.line;
        perr(p, "union discriminant must be an integer, bool or enum");
    }
    expect_punct(p, ')');
    expect_punct(p, '{');

    while (is_kw(p, "case")) {
        oncRPCXCase c;
        memset(&c, 0, sizeof(c));
        do {
            const char *v;
            lex(p);   /* 'case' */
            v = parse_value(p);
            c.values = (const char **)pgrow(p, (void *)c.values, c.nvalues, sizeof(*c.values));
            c.values[c.nvalues++] = v;
            expect_punct(p, ':');
        } while (is_kw(p, "case"));
        parse_decl(p, &c.arm);
        name_inline_field(p, &c.arm, def->name);
        if (c.arm.kind != ONCRPC_XDECL_VOID) {
            check_dup_field(p, arms, narms, &c.arm);
            if (def->disc.name != NULL && strcmp(c.arm.name, def->disc.name) == 0)
                perr(p, "union arm '%s' has the same name as the discriminant", c.arm.name);
            arms = (oncRPCXDecl *)pgrow(p, arms, narms, sizeof(*arms));
            arms[narms++] = c.arm;
        }
        expect_punct(p, ';');
        def->cases = (oncRPCXCase *)pgrow(p, def->cases, def->ncases, sizeof(c));
        def->cases[def->ncases++] = c;
    }
    if (def->ncases == 0)
        expected(p, "'case'");
    if (is_kw(p, "default")) {
        lex(p);
        expect_punct(p, ':');
        def->defaultArm = (oncRPCXDecl *)palloc(p, sizeof(oncRPCXDecl));
        parse_decl(p, def->defaultArm);
        name_inline_field(p, def->defaultArm, def->name);
        if (def->defaultArm->kind != ONCRPC_XDECL_VOID)
            check_dup_field(p, arms, narms, def->defaultArm);
        expect_punct(p, ';');
    }
    expect_punct(p, '}');
}

static oncRPCXType parse_proc_type(parser *p)
{
    oncRPCXType t;
    memset(&t, 0, sizeof(t));
    if (is_kw(p, "void")) {
        lex(p);
        t.kind = ONCRPC_XTYPE_VOID;
        return t;
    }
    if (is_kw(p, "string")) {
        lex(p);
        t.kind = ONCRPC_XTYPE_STRING;
        return t;
    }
    return parse_type_spec(p, 0);
}

static void parse_program(parser *p)
{
    oncRPCXDef *def;
    int line = p->tline;

    lex(p);   /* 'program' */
    def = new_def(p, ONCRPC_XDEF_PROGRAM, expect_ident(p), line);
    declare_name(p, def->name, line);
    expect_punct(p, '{');
    do {
        oncRPCXVersion v;
        int vline = p->tline;
        size_t i;

        memset(&v, 0, sizeof(v));
        expect_kw(p, "version");
        v.name = expect_ident(p);
        declare_name(p, v.name, vline);
        expect_punct(p, '{');
        do {
            oncRPCXProc pr;
            size_t j;

            memset(&pr, 0, sizeof(pr));
            pr.line = p->tline;
            pr.result = parse_proc_type(p);
            pr.name = expect_ident(p);
            expect_punct(p, '(');
            if (is_kw(p, "void")) {
                lex(p);
            } else {
                for (;;) {
                    oncRPCXType a = parse_proc_type(p);
                    if (a.kind == ONCRPC_XTYPE_VOID)
                        perr(p, "'void' must be the only argument");
                    pr.args = (oncRPCXType *)pgrow(p, pr.args, pr.nargs, sizeof(a));
                    pr.args[pr.nargs++] = a;
                    if (!is_punct(p, ','))
                        break;
                    lex(p);
                }
            }
            expect_punct(p, ')');
            expect_punct(p, '=');
            pr.number = parse_value(p);
            expect_punct(p, ';');
            for (j = 0; j < v.nprocs; j++) {
                if (strcmp(v.procs[j].name, pr.name) == 0) {
                    p->tline = pr.line;
                    perr(p, "procedure '%s' defined twice in version '%s'", pr.name, v.name);
                }
            }
            declare_proc(p, pr.name, pr.number, pr.line);
            v.procs = (oncRPCXProc *)pgrow(p, v.procs, v.nprocs, sizeof(pr));
            v.procs[v.nprocs++] = pr;
        } while (!is_punct(p, '}'));
        lex(p);
        expect_punct(p, '=');
        v.number = parse_value(p);
        expect_punct(p, ';');
        for (i = 0; i < def->nversions; i++) {
            if (strcmp(def->versions[i].number, v.number) == 0) {
                p->tline = vline;
                perr(p, "version number %s used twice in program '%s'", v.number, def->name);
            }
        }
        def->versions = (oncRPCXVersion *)pgrow(p, def->versions, def->nversions, sizeof(v));
        def->versions[def->nversions++] = v;
    } while (!is_punct(p, '}'));
    lex(p);
    expect_punct(p, '=');
    def->value = parse_value(p);
    expect_punct(p, ';');
    append_def(p, def);
}

static void parse_definition(parser *p)
{
    int line = p->tline;

    if (p->tk == TK_PASSTHRU) {
        oncRPCXDef *d = new_def(p, ONCRPC_XDEF_PASSTHROUGH, NULL, line);
        d->value = p->text;
        append_def(p, d);
        lex(p);
        return;
    }
    if (is_kw(p, "const")) {
        oncRPCXDef *d;
        lex(p);
        d = new_def(p, ONCRPC_XDEF_CONST, expect_ident(p), line);
        declare_name(p, d->name, line);
        expect_punct(p, '=');
        d->value = parse_value(p);
        expect_punct(p, ';');
        append_def(p, d);
        return;
    }
    if (is_kw(p, "typedef")) {
        oncRPCXDecl dc;
        oncRPCXDef *d;
        lex(p);
        parse_decl(p, &dc);
        if (dc.kind == ONCRPC_XDECL_VOID) {
            p->tline = line;
            perr(p, "cannot typedef 'void'");
        }
        if (dc.type.inlineDef != NULL && dc.type.inlineDef->anonymous) {
            if (dc.kind == ONCRPC_XDECL_PLAIN) {
                /* typedef struct { ... } name;  is simply  struct name { ... }; */
                name_inline(p, &dc, dc.name);
                expect_punct(p, ';');
                return;
            }
            name_inline(p, &dc, pstrcat3(p, dc.name, "_", "elem"));
        }
        declare_name(p, dc.name, line);
        d = new_def(p, ONCRPC_XDEF_TYPEDEF, dc.name, line);
        d->decl = dc;
        expect_punct(p, ';');
        append_def(p, d);
        return;
    }
    if (is_kw(p, "struct") || is_kw(p, "union") || is_kw(p, "enum")) {
        oncRPCXType t = parse_type_spec(p, 1);
        if (t.inlineDef == NULL) {
            p->tline = line;
            perr(p, "expected a type definition");
        }
        if (t.inlineDef->anonymous) {
            p->tline = line;
            perr(p, "top-level type definition needs a name");
        }
        expect_punct(p, ';');
        return;
    }
    if (is_kw(p, "program")) {
        parse_program(p);
        return;
    }
    expected(p, "definition ('const', 'typedef', 'enum', 'struct', 'union' or 'program')");
}

/* ---- Semantic checks ------------------------------------------------------ */

static const oncRPCXDef *find_def(const oncRPCXSpec *spec, const char *name)
{
    const oncRPCXDef *d;
    for (d = spec->defs; d != NULL; d = d->next)
        if (d->name != NULL && strcmp(d->name, name) == 0)
            return d;
    return NULL;
}

static void check_type(parser *p, const oncRPCXType *t, int line)
{
    const oncRPCXDef *d;
    if (t->kind != ONCRPC_XTYPE_NAMED)
        return;
    d = find_def(p->spec, t->name);
    if (d == NULL || d->kind == ONCRPC_XDEF_CONST || d->kind == ONCRPC_XDEF_PROGRAM) {
        p->tline = line;
        perr(p, "undefined type '%s'", t->name);
    }
}

static void check_decl(parser *p, const oncRPCXDecl *d)
{
    if (d->kind == ONCRPC_XDECL_PLAIN || d->kind == ONCRPC_XDECL_FIXED_ARRAY ||
        d->kind == ONCRPC_XDECL_VAR_ARRAY || d->kind == ONCRPC_XDECL_OPTIONAL)
        check_type(p, &d->type, d->line);
}

static void check_types(parser *p)
{
    const oncRPCXDef *d;
    size_t i, j, k;

    for (d = p->spec->defs; d != NULL; d = d->next) {
        switch (d->kind) {
        case ONCRPC_XDEF_TYPEDEF:
            check_decl(p, &d->decl);
            break;
        case ONCRPC_XDEF_STRUCT:
            for (i = 0; i < d->nfields; i++)
                check_decl(p, &d->fields[i]);
            break;
        case ONCRPC_XDEF_UNION:
            check_decl(p, &d->disc);
            if (d->disc.type.kind == ONCRPC_XTYPE_NAMED) {
                const oncRPCXDef *dt = find_def(p->spec, d->disc.type.name);
                if (dt->kind != ONCRPC_XDEF_ENUM && dt->kind != ONCRPC_XDEF_TYPEDEF) {
                    p->tline = d->disc.line;
                    perr(p, "union discriminant must be an integer, bool or enum");
                }
            }
            for (i = 0; i < d->ncases; i++)
                check_decl(p, &d->cases[i].arm);
            if (d->defaultArm != NULL)
                check_decl(p, d->defaultArm);
            break;
        case ONCRPC_XDEF_PROGRAM:
            for (i = 0; i < d->nversions; i++) {
                const oncRPCXVersion *v = &d->versions[i];
                for (j = 0; j < v->nprocs; j++) {
                    check_type(p, &v->procs[j].result, v->procs[j].line);
                    for (k = 0; k < v->procs[j].nargs; k++)
                        check_type(p, &v->procs[j].args[k], v->procs[j].line);
                }
            }
            break;
        default:
            break;
        }
    }
}

/* ---- Public API ------------------------------------------------------------ */

oncRPCXSpec *oncRPCXSpecParse(const oncRPCAllocator *alloc, const char *text,
                              const char *filename, char *err, size_t errlen)
{
    parser p;
    arena *volatile ar;
    oncRPCXSpec *volatile spec;

    if (err != NULL && errlen > 0)
        err[0] = '\0';
    if (text == NULL || alloc == NULL)
        return NULL;
    ar = (arena *)oncRPCAlloc(alloc, sizeof(arena));
    spec = (oncRPCXSpec *)oncRPCAlloc(alloc, sizeof(oncRPCXSpec));
    if (ar == NULL || spec == NULL) {
        oncRPCFree(alloc, ar);
        oncRPCFree(alloc, spec);
        if (err != NULL && errlen > 0)
            snprintf(err, errlen, "out of memory");
        return NULL;
    }
    ar->alloc = alloc;
    spec->alloc = alloc;
    memset(&p, 0, sizeof(p));
    p.src = text;
    p.line = 1;
    p.bol = 1;
    p.ar = ar;
    p.spec = spec;
    p.err = err;
    p.errlen = errlen;
    p.fname = filename != NULL ? filename : "<input>";

    if (setjmp(p.jb) != 0) {
        arena_release(ar);
        oncRPCFree(alloc, ar);
        oncRPCFree(alloc, spec);
        return NULL;
    }
    spec->filename = pstrndup(&p, p.fname, strlen(p.fname));
    lex(&p);
    while (p.tk != TK_EOF)
        parse_definition(&p);
    check_types(&p);
    spec->priv = ar;
    return spec;
}

void oncRPCXSpecFree(oncRPCXSpec *spec)
{
    const oncRPCAllocator *a;
    if (spec == NULL)
        return;
    a = spec->alloc;
    if (spec->priv != NULL) {
        arena_release((arena *)spec->priv);
        oncRPCFree(a, spec->priv);
    }
    oncRPCFree(a, spec);
}

const oncRPCXDef *oncRPCXSpecFind(const oncRPCXSpec *spec, const char *name)
{
    if (spec == NULL || name == NULL)
        return NULL;
    return find_def(spec, name);
}

static const oncRPCXEnumItem *find_enum_item(const oncRPCXSpec *spec, const char *name)
{
    const oncRPCXDef *d;
    size_t i;
    for (d = spec->defs; d != NULL; d = d->next)
        if (d->kind == ONCRPC_XDEF_ENUM)
            for (i = 0; i < d->nitems; i++)
                if (strcmp(d->items[i].name, name) == 0)
                    return &d->items[i];
    return NULL;
}

static int eval_depth(const oncRPCXSpec *spec, const char *value, int64_t *out, int depth)
{
    const oncRPCXDef *d;

    if (value == NULL || depth > 64)
        return -1;
    if (isdigit((unsigned char)value[0]) || value[0] == '-') {
        char *end;
        long long v;
        errno = 0;
        v = strtoll(value, &end, 0);
        if (*end != '\0' || errno != 0)
            return -1;
        *out = (int64_t)v;
        return 0;
    }
    if (find_def(spec, value) == NULL && find_enum_item(spec, value) == NULL) {
        if (strcmp(value, "TRUE") == 0) {
            *out = 1;
            return 0;
        }
        if (strcmp(value, "FALSE") == 0) {
            *out = 0;
            return 0;
        }
    }
    for (d = spec->defs; d != NULL; d = d->next) {
        if (d->kind == ONCRPC_XDEF_CONST && strcmp(d->name, value) == 0)
            return eval_depth(spec, d->value, out, depth + 1);
        if (d->kind == ONCRPC_XDEF_ENUM) {
            int64_t cur = -1;
            size_t i;
            for (i = 0; i < d->nitems; i++) {
                if (d->items[i].value != NULL) {
                    if (eval_depth(spec, d->items[i].value, &cur, depth + 1) != 0)
                        cur = INT64_MIN;
                } else if (cur != INT64_MIN) {
                    cur++;
                }
                if (strcmp(d->items[i].name, value) == 0) {
                    if (cur == INT64_MIN)
                        return -1;
                    *out = cur;
                    return 0;
                }
            }
        }
    }
    return -1;
}

int oncRPCXSpecEval(const oncRPCXSpec *spec, const char *value, int64_t *out)
{
    if (spec == NULL || out == NULL)
        return -1;
    return eval_depth(spec, value, out, 0);
}
