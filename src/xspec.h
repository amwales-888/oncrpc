/*
 * xspec.h - parser for the XDR/RPC language (.x files) and the
 * C code generator used by the oncrpcgen command.
 *
 * The language follows RFC 4506 section 6 (XDR) and RFC 5531 section 12
 * (program definitions), with these common extensions:
 *   - lines starting with '%' are copied verbatim into the generated header
 *   - lines starting with '#' (preprocessor directives) are ignored
 *   - C and C++ style comments
 *   - char, short, long and bare 'unsigned' type names
 *   - enum values without "= value" continue from the previous value
 *   - 'string' as a procedure argument or result type
 *   - inline (anonymous) struct/union/enum definitions are hoisted to
 *     named types: "<parent>_<field>"
 *   - "struct name", "union name", "enum name" as type references
 * 'quadruple' is recognised but rejected (no portable C type).
 */
#ifndef ONCRPC_XSPEC_H
#define ONCRPC_XSPEC_H

#include <stddef.h>
#include <stdint.h>

#include "xdr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum oncRPCXTypeKind {
    ONCRPC_XTYPE_VOID = 0,
    ONCRPC_XTYPE_INT,       /* int, long        -> int32_t   */
    ONCRPC_XTYPE_UINT,      /* unsigned int     -> uint32_t  */
    ONCRPC_XTYPE_HYPER,     /* hyper            -> int64_t   */
    ONCRPC_XTYPE_UHYPER,    /* unsigned hyper   -> uint64_t  */
    ONCRPC_XTYPE_FLOAT,
    ONCRPC_XTYPE_DOUBLE,
    ONCRPC_XTYPE_BOOL,      /* bool             -> oncRPCBool */
    ONCRPC_XTYPE_CHAR,      /* char             -> int8_t    */
    ONCRPC_XTYPE_UCHAR,     /* unsigned char    -> uint8_t   */
    ONCRPC_XTYPE_SHORT,     /* short            -> int16_t   */
    ONCRPC_XTYPE_USHORT,    /* unsigned short   -> uint16_t  */
    ONCRPC_XTYPE_STRING,    /* procedure arguments/results only */
    ONCRPC_XTYPE_NAMED      /* a typedef, enum, struct or union */
} oncRPCXTypeKind;

struct oncRPCXDef;

typedef struct oncRPCXType {
    oncRPCXTypeKind   kind;
    const char         *name;        /* ONCRPC_XTYPE_NAMED */
    struct oncRPCXDef *inlineDef;  /* set if defined inline */
} oncRPCXType;

typedef enum oncRPCXDeclKind {
    ONCRPC_XDECL_VOID = 0,      /* void                   */
    ONCRPC_XDECL_PLAIN,         /* type name              */
    ONCRPC_XDECL_FIXED_ARRAY,   /* type name[size]        */
    ONCRPC_XDECL_VAR_ARRAY,     /* type name<size>        */
    ONCRPC_XDECL_OPAQUE_FIXED,  /* opaque name[size]      */
    ONCRPC_XDECL_OPAQUE_VAR,    /* opaque name<size>      */
    ONCRPC_XDECL_STRING,        /* string name<size>      */
    ONCRPC_XDECL_OPTIONAL       /* type *name             */
} oncRPCXDeclKind;

typedef struct oncRPCXDecl {
    oncRPCXDeclKind kind;
    oncRPCXType      type;
    const char       *name;
    const char       *size;   /* array/opaque/string bound; NULL = unbounded */
    int               line;
} oncRPCXDecl;

typedef struct oncRPCXEnumItem {
    const char *name;
    const char *value;        /* NULL: previous value + 1 (first: 0) */
} oncRPCXEnumItem;

typedef struct oncRPCXCase {
    const char  **values;
    size_t        nvalues;
    oncRPCXDecl  arm;
} oncRPCXCase;

typedef struct oncRPCXProc {
    const char   *name;
    const char   *number;
    oncRPCXType  result;
    oncRPCXType *args;       /* nargs == 0 for void */
    size_t        nargs;
    int           line;
} oncRPCXProc;

typedef struct oncRPCXVersion {
    const char   *name;
    const char   *number;
    oncRPCXProc *procs;
    size_t        nprocs;
} oncRPCXVersion;

typedef enum oncRPCXDefKind {
    ONCRPC_XDEF_CONST,
    ONCRPC_XDEF_TYPEDEF,
    ONCRPC_XDEF_ENUM,
    ONCRPC_XDEF_STRUCT,
    ONCRPC_XDEF_UNION,
    ONCRPC_XDEF_PROGRAM,
    ONCRPC_XDEF_PASSTHROUGH
} oncRPCXDefKind;

/* One top-level definition. Which fields are used depends on kind. */
typedef struct oncRPCXDef {
    oncRPCXDefKind    kind;
    const char         *name;       /* not used for PASSTHROUGH */
    int                 line;
    int                 anonymous;  /* internal: inline type awaiting a name */
    const char         *value;      /* CONST value, PROGRAM number, PASSTHROUGH text */
    oncRPCXDecl        decl;       /* TYPEDEF */
    oncRPCXEnumItem  *items;      /* ENUM */
    size_t              nitems;
    oncRPCXDecl       *fields;     /* STRUCT */
    size_t              nfields;
    oncRPCXDecl        disc;       /* UNION discriminant */
    oncRPCXCase       *cases;      /* UNION */
    size_t              ncases;
    oncRPCXDecl       *defaultArm;/* UNION, NULL if none */
    oncRPCXVersion    *versions;   /* PROGRAM */
    size_t              nversions;
    struct oncRPCXDef *next;
} oncRPCXDef;

typedef struct oncRPCXSpec {
    oncRPCXDef            *defs;      /* in source order (inline types precede users) */
    const char            *filename;
    const oncRPCAllocator *alloc;
    void                  *priv;      /* internal */
} oncRPCXSpec;

/*
 * Parse the specification in text (NUL-terminated). filename is only used
 * in messages. All memory, including the returned structure, comes from
 * alloc. On error NULL is returned and a "file:line: error: message" text
 * is written to err. Parsing is pure computation and never blocks.
 */
ONCRPC_API oncRPCXSpec *oncRPCXSpecParse(const oncRPCAllocator *alloc, const char *text,
                                         const char *filename, char *err, size_t errlen);
ONCRPC_API void         oncRPCXSpecFree(oncRPCXSpec *spec);

/* Find a definition (const, type or program) by name. */
ONCRPC_API const oncRPCXDef *oncRPCXSpecFind(const oncRPCXSpec *spec, const char *name);
/* Evaluate a numeric literal, constant name or enum item. 0 on success. */
ONCRPC_API int oncRPCXSpecEval(const oncRPCXSpec *spec, const char *value, int64_t *out);

typedef enum oncRPCXGenKind {
    ONCRPC_GEN_HEADER,           /* <base>.h         types, constants, prototypes */
    ONCRPC_GEN_XDR,              /* <base>_xdr.c     XDR routines                 */
    ONCRPC_GEN_CLIENT,           /* <base>_clnt.c    client stubs                 */
    ONCRPC_GEN_SERVER,           /* <base>_svc.c     server dispatch/registration */
    ONCRPC_GEN_SERVER_SKELETON,  /* <base>_server_skel.c  implementation template */
    ONCRPC_GEN_CLIENT_SKELETON   /* <base>_client_skel.c  example client          */
} oncRPCXGenKind;

/*
 * Generate one output file into memory. basename names the header the
 * generated .c files include ("<basename>.h"). On success *out holds the
 * NUL-terminated text (*outlen bytes, excluding the NUL), allocated from the
 * specification's allocator: release it with oncRPCFree. Returns 0 on
 * success, -1 on error (message in err).
 */
ONCRPC_API int oncRPCXSpecGenerate(const oncRPCXSpec *spec, oncRPCXGenKind kind,
                                   const char *basename, char **out, size_t *outlen,
                                   char *err, size_t errlen);

#ifdef __cplusplus
}
#endif

#endif /* ONCRPC_XSPEC_H */
