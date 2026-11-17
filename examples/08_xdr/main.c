/*
 * 08_xdr/main.c - XDR without RPC: encode data to bytes and back.
 *
 * usage: xdr_demo
 *
 * The same routine, xdr_reading_log(), encodes, decodes and frees,
 * depending on the stream it is given:
 *
 *   encode stream: C structures -> bytes (big-endian, 4-byte aligned)
 *   decode stream: bytes -> C structures (strings and arrays are allocated)
 *   free stream:   releases what decoding allocated (xdr_free_reading_log)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "readings.h"

static void *app_realloc(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    return realloc(ptr, size);
}

static void app_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static const oncRPCAllocator app_alloc = { app_realloc, app_free, NULL };

static const char *level_name(alert_level level)
{
    switch (level) {
    case LEVEL_NORMAL:  return "normal";
    case LEVEL_WARNING: return "warning";
    case LEVEL_ALARM:   return "alarm";
    }
    return "?";
}

static void print_log(const char *title, const reading_log *log)
{
    uint32_t i;
    printf("%s (%u readings)\n", title, (unsigned)log->reading_log_len);
    for (i = 0; i < log->reading_log_len; i++) {
        const reading *r = &log->reading_log_val[i];
        printf("  %-12s t=%lld value=%-7.2f %s\n", r->sensor, (long long)r->timestamp,
               r->value, level_name(r->level));
    }
}

int main(void)
{
    reading items[3];
    reading_log original, copy;
    oncRPCXdr encoder, decoder;
    const uint8_t *bytes;
    size_t length, i;
    int ok;

    /* ---- Some data to store -------------------------------------------- */
    items[0].sensor = "boiler";
    items[0].timestamp = 1763200000000LL;
    items[0].value = 71.5;
    items[0].level = LEVEL_NORMAL;
    items[1].sensor = "pressure";
    items[1].timestamp = 1763200000500LL;
    items[1].value = 2.95;
    items[1].level = LEVEL_WARNING;
    items[2].sensor = "flue";
    items[2].timestamp = 1763200001000LL;
    items[2].value = 412.0;
    items[2].level = LEVEL_ALARM;
    original.reading_log_len = 3;
    original.reading_log_val = items;
    print_log("original", &original);

    /* ---- Encode into a buffer that grows as needed ---------------------- */
    if (!oncRPCXdrInitEncodeDynamic(&encoder, &app_alloc, 256)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if (!xdr_reading_log(&encoder, &original)) {
        fprintf(stderr, "encoding failed\n");
        oncRPCXdrDestroy(&encoder);
        return 1;
    }
    bytes = oncRPCXdrData(&encoder);
    length = oncRPCXdrLength(&encoder);
    printf("\nencoded to %u bytes; the first reading:\n ", (unsigned)length);
    for (i = 0; i < 36 && i < length; i++)
        printf(" %02x%s", bytes[i], (i % 16 == 15) ? "\n " : "");
    printf("\n  (count 3, string length 6 \"boiler\" + 2 padding bytes, 8-byte hyper,"
           " 8-byte double, 4-byte enum)\n\n");

    /* These bytes could now be written to a file or sent anywhere. */

    /* ---- Decode them back ------------------------------------------------ */
    memset(&copy, 0, sizeof(copy));   /* decode targets start zeroed */
    oncRPCXdrInitDecode(&decoder, &app_alloc, bytes, length);
    ok = xdr_reading_log(&decoder, &copy);
    if (!ok) {
        fprintf(stderr, "decoding failed\n");
        xdr_free_reading_log(&app_alloc, &copy);   /* frees any partial result */
        oncRPCXdrDestroy(&encoder);
        return 1;
    }
    print_log("decoded", &copy);
    xdr_free_reading_log(&app_alloc, &copy);       /* the array and the strings */

    /* ---- Damaged input is rejected, not trusted ---------------------------- */
    memset(&copy, 0, sizeof(copy));
    oncRPCXdrInitDecode(&decoder, &app_alloc, bytes, length - 5);
    ok = xdr_reading_log(&decoder, &copy);
    printf("\ndecoding all but the last 5 bytes: %s\n", ok ? "accepted (unexpected!)" : "rejected");
    xdr_free_reading_log(&app_alloc, &copy);

    oncRPCXdrDestroy(&encoder);
    return ok ? 1 : 0;
}
