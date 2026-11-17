/*
 * add.x - the smallest useful RPC interface: add two integers.
 *
 * A program has a number (any value in 0x20000000-0x3fffffff is free for
 * your own use) and one or more versions. Each version lists procedures:
 *
 *     result-type NAME(argument-types) = procedure-number;
 */
program ADD_PROG {
    version ADD_VERS {
        int ADD(int, int) = 1;
    } = 1;
} = 0x20000101;
