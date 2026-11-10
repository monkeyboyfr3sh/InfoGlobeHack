#pragma once

/* Live values in scene text: "CPU {var:cpu} {clock}" -> "CPU 42 07:05 PM".
 *
 * The template syntax, the order in which names are looked up and the
 * pushed-value formats ({var:cpu}, {bar:cpu}, {pct:print:250}) live here;
 * the time-based values are in clock_values.c. Pure C like that file, so
 * hello_world/host_test runs it on a PC against web/src/lib/values.js.
 * The globe supplies its clock, IP address and value store through env. */

#include <stdbool.h>
#include <stddef.h>
#include "clock_values.h"

#define LIVE_KEY_MAX 16     // pushed value names, including the NUL (= VALUE_KEY_MAX)

typedef struct {
    const clock_now_t *now;
    bool (*get)(const char *key, char *out, size_t cap);   // a pushed value; false if not set
    void (*ip)(char *out, size_t cap);                     // {ip}
} live_env_t;

/* One value by name and optional arg ({clock:24} -> "clock", "24").
 * False for an unknown name (it then stays in the text as typed). */
bool live_value(const char *name, const char *arg, const live_env_t *env, char *out, size_t cap);

/* Fill in every value in tmpl. *live (may be NULL) is set when the text
 * contained any value, i.e. it can change over time. */
void live_subst(const char *tmpl, const live_env_t *env, char *out, size_t cap, bool *live);
