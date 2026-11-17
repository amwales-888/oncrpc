/*
 * readings.x - data types only, no program.
 *
 * XDR is useful on its own: a portable, compact binary format for files,
 * messages or caches. With no "program" in the file, oncrpcgen produces
 * only types and XDR routines, and the code needs no libuv.
 */
const MAX_SENSOR = 32;

enum alert_level {
    LEVEL_NORMAL  = 0,
    LEVEL_WARNING = 1,
    LEVEL_ALARM   = 2
};

struct reading {
    string      sensor<MAX_SENSOR>;
    hyper       timestamp;     /* milliseconds since 1970 */
    double      value;
    alert_level level;
};

typedef reading reading_log<>;
