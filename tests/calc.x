/*
 * calc.x - calculator interface used by tests/test_rpc.c.
 *
 * Shows the main features of the XDR/RPC language: constants, enums,
 * structs, discriminated unions, bounded strings, variable-length arrays,
 * optional data (a linked list), and procedures with zero, one or several
 * arguments.
 *
 * Generated into build/tests/calc by make.
 */

%/* Lines starting with '%' are copied into the generated header. */

const MAX_NAME   = 64;
const MAX_VALUES = 1024;

enum calc_op {
    OP_ADD = 0,
    OP_SUB = 1,
    OP_MUL = 2,
    OP_DIV = 3
};

struct calc_args {
    calc_op op;
    hyper   a;
    hyper   b;
};

/* Either a value or an error message. */
union calc_result switch (int status) {
case 0:
    hyper value;
case 1:
    string error<MAX_NAME>;
default:
    void;
};

typedef double value_list<MAX_VALUES>;

struct stats {
    unsigned int count;
    double       mean;
    double       min;
    double       max;
};

/* A singly linked list of names. */
struct name_node {
    string     name<MAX_NAME>;
    name_node *next;
};
typedef name_node *name_list;

program CALC_PROG {
    version CALC_VERS {
        void        CALC_NULL(void)        = 0;
        calc_result CALC_COMPUTE(calc_args) = 1;
        stats       CALC_STATS(value_list)  = 2;
        string      CALC_ECHO(string)       = 3;
        name_list   CALC_REVERSE(name_list) = 4;
        int         CALC_ADD(int, int)      = 5;
    } = 1;
} = 0x20000099;
