/*
 * delay.x - a procedure whose answer takes time.
 *
 * DELAY(ms) replies after ms milliseconds with the same number.
 */
program DELAY_PROG {
    version DELAY_VERS {
        unsigned int DELAY(unsigned int) = 1;
    } = 1;
} = 0x20000105;
