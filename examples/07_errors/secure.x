/*
 * secure.x - credentials and error handling.
 */
program SECURE_PROG {
    version SECURE_VERS {
        string WHOAMI(void)     = 1;   /* describes the caller's credentials  */
        void   ADMIN_ONLY(void) = 2;   /* only uid 0 is allowed               */
        void   NO_REPLY(void)   = 3;   /* the server never answers this one   */
    } = 1;
} = 0x20000107;
