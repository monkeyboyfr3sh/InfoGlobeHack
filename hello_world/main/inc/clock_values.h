#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/time.h>

/* Clock-studio values: name (and optional arg after ':') -> text.
 * Returns false for names it doesn't know. */
bool clock_value(const char *name, const char *arg, const struct timeval *tv, char *out, size_t cap);

/* Location for {sunrise}/{sunset}; unset until settings provide it. */
void clock_values_set_location(double lat, double lon);
bool clock_values_get_location(double *lat, double *lon);
