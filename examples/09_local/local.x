/*
 * local.x - a service reached through a Unix domain socket (a named pipe on
 * Windows) instead of a network address.
 */

program LOCAL_PROG {
    version LOCAL_VERS {
        /* Who does the server see calling? */
        string WHO_AM_I(void) = 1;
    } = 1;
} = 0x20000009;
