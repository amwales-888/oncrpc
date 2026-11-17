/*
 * alltypes.x - exercises every declaration form the generator supports.
 * Used by tests/test_gen.c.
 */
const SMALL = 4;
const NAMELEN = 32;

enum shape { CIRCLE = 1, SQUARE, TRIANGLE = 10 };

typedef opaque handle[8];
typedef opaque blob<>;
typedef string label<NAMELEN>;
typedef int ints<SMALL>;
typedef unsigned hyper words[3];

struct point {
    float x;
    float y;
};

typedef point *point_ptr;

struct everything {
    int             i;
    unsigned int    u;
    hyper           h;
    unsigned hyper  uh;
    float           f;
    double          d;
    bool            b;
    char            c;
    unsigned char   uc;
    short           s;
    unsigned short  us;
    shape           sh;
    int             fixed_ints[SMALL];
    point           pts<>;
    point           some_pts<2>;
    opaque          raw[5];
    opaque          var_raw<16>;
    string          name<NAMELEN>;
    label           tag;
    handle          fh;
    blob            data;
    ints            numbers;
    words           w;
    point          *maybe;
    point_ptr       maybe2;
    label           labels<>;
    struct { int depth; string why<>; } nested;
};

union shape_info switch (shape kind) {
case CIRCLE:
    double radius;
case SQUARE:
case TRIANGLE:
    point corners<>;
default:
    void;
};

union flag switch (bool set) {
case TRUE:
    unsigned int value;
case FALSE:
    void;
};

struct list {
    shape_info item;
    list      *next;
};

program ALLTYPES_PROG {
    version ALLTYPES_V1 {
        void       PING(void) = 0;
        everything ROUNDTRIP(everything) = 1;
        list       LISTS(list, shape_info, string) = 2;
        string     NAMES(label) = 3;
        handle     HANDLE(void) = 4;
        void       NOTHING(int) = 5;
    } = 1;
    version ALLTYPES_V2 {
        void       PING(void) = 0;
        bool       CHECK(unsigned hyper) = 1;
    } = 2;
} = 0x20000123;
