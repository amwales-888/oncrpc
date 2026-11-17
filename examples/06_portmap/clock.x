/*
 * clock.x - a time service found through the port mapper.
 */
program CLOCK_PROG {
    version CLOCK_VERS {
        unsigned hyper GET_TIME(void) = 1;   /* seconds since 1970 */
    } = 1;
} = 0x20000106;
