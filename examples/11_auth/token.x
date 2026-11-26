/*
 * token.x - a service that checks its callers with a made-up credential,
 * and answers one procedure later.
 */
program TOKEN_PROG {
    version TOKEN_VERS {
        string       WHOAMI(void)          = 1;   /* the user the server let in       */
        string       SLOW_ECHO(string)     = 2;   /* answered 300 ms later            */
        unsigned int COUNT_ABANDONED(void) = 3;   /* SLOW_ECHOs whose caller went away */
    } = 1;
} = 0x20000111;
