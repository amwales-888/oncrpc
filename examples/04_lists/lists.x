/*
 * lists.x - variable-length arrays and linked lists.
 *
 * "T name<N>" is an array of up to N elements. In C it becomes a struct
 * with name_len (the count) and name_val (a pointer to the elements).
 *
 * "T *name" is optional data: a pointer that may be NULL. A struct holding
 * a pointer to its own type makes a linked list.
 */
const MAX_SAMPLES = 1000;
const MAX_WORD    = 32;

typedef double samples<MAX_SAMPLES>;

struct summary {
    unsigned int count;
    double       min;
    double       max;
    double       mean;
};

struct word {
    string text<MAX_WORD>;
    word  *next;
};
typedef word *word_list;

program LISTS_PROG {
    version LISTS_VERS {
        summary   SUMMARIZE(samples)    = 1;
        word_list SORT_WORDS(word_list) = 2;
    } = 1;
} = 0x20000104;
