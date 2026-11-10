#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

/* Clock-studio values: name (and optional arg after ':') -> text.
 * Returns false for names it doesn't know. */
bool clock_value(const char *name, const char *arg, const struct timeval *tv, char *out, size_t cap);

/* World clock ({tz:tyo}): the city's offset from UTC at `utc`, DST included.
 * Pure computation (no TZ swapping). False for an unknown city key. */
bool clock_city_offset(const char *key, int64_t utc, int32_t *offset);

/* Location for {sunrise}/{sunset}; unset until settings provide it. */
void clock_values_set_location(double lat, double lon);
bool clock_values_get_location(double *lat, double *lon);
