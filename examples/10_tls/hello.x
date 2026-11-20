/*
 * hello.x - a greeting, over TLS.
 */

program HELLO_PROG {
    version HELLO_VERS {
        /* Greets the caller, saying who its certificate says it is. */
        string HELLO(string) = 1;
    } = 1;
} = 0x2000000a;
