/*
 * greet.x - strings in and out.
 *
 * "string" is a NUL-terminated char * in C. A bound such as <MAX_TEXT>
 * limits its length; the library rejects longer strings in both directions.
 */
const MAX_TEXT = 100;

typedef string text<MAX_TEXT>;

program GREET_PROG {
    version GREET_VERS {
        text HELLO(text) = 1;   /* "Ada" -> "Hello, Ada!"   */
        text SHOUT(text) = 2;   /* "Hi"  -> "HI"            */
    } = 1;
} = 0x20000102;
