#pragma once

/* Live values that depend only on the time: {clock}, {date}, {fuzzy}, {moon},
 * {tz:tyo}, {until:12-25}, {bar:day} ... (docs/live-values.md).
 *
 * Pure C: no FreeRTOS, ESP-IDF or C-library time-zone calls, so the same code
 * is compiled on a PC by hello_world/host_test and checked against the
 * designer's preview (web/src/lib/values.js) with shared test vectors during
 * `npm test`. The caller supplies the instant and the local UTC offset; on
 * the globe the offset comes from newlib's localtime_r() and the TZ rule. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    int64_t   utc;            // seconds since 1970-01-01 UTC
    int32_t   usec;           // 0-999999
    int32_t   offset;         // local time minus UTC in seconds, DST included
    struct tm tm;             // local time, filled by clock_now_set()
    /* Colon blink of {clock} {time} {time24}. Without colon_set (the frame's
     * "blinkColon"), {clock} keeps its classic one-second blink and the
     * others are steady. */
    bool      steady;         // no blinking at all: tells a real change from a blink
    bool      colon_set;
    uint16_t  colon_period;   // full on+off cycle in ms; 0 = steady
    char      colon_look;     // what the colon becomes in the off half: ' ' or '.'
} clock_now_t;

/* Set the instant; fills c->tm from utc + offset. Leaves the colon fields. */
void clock_now_set(clock_now_t *c, int64_t utc, int32_t usec, int32_t offset);

/* A broken-down time read as UTC, in seconds since 1970 (like timegm()).
 * With localtime_r()'s result it gives the local offset: secs(&lt) - now. */
int64_t clock_tm_seconds(const struct tm *t);

/* name (and optional arg after ':') -> text. False for names it doesn't know. */
bool clock_value(const char *name, const char *arg, const clock_now_t *c, char *out, size_t cap);

/* World clock ({tz:tyo}): the city's offset from UTC at `utc`, DST included.
 * False for an unknown city key. */
bool clock_city_offset(const char *key, int64_t utc, int32_t *offset);

/* Location for {sunrise}/{sunset}; unset (NAN) until settings provide it. */
void clock_values_set_location(double lat, double lon);
bool clock_values_get_location(double *lat, double *lon);
