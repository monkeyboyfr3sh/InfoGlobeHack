#pragma once

/* One blocking HTTP(S) GET for the feeds (weather, aircraft, ...).
 *
 * Returns the body as a NUL-terminated heap string (caller frees), or NULL on
 * a network error, a non-2xx status, or a body longer than max_bytes.
 * *status (may be NULL) gets the HTTP status, or 0 when there was none.
 * HTTPS is checked against the built-in certificate bundle. Call it from a
 * task with at least 6 KB of stack; never from the display or HTTP server task. */

#include <stddef.h>

char *http_fetch(const char *url, size_t max_bytes, size_t *len, int *status);
