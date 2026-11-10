#pragma once

/* Pushed values: a small key -> text table in RAM that scene text can show.
 *
 *   {var:cpu}        the value, or "--" when nothing has set it (or it expired)
 *   {cpu}            the value, for any name that isn't a built-in one; while it
 *                    is not set the text stays as typed, so prefer {var:cpu}
 *   {bar:cpu}        10-character bar of a 0-100 number; {bar:print:250} = out of 250
 *   {pct:print:250}  whole percent of the same
 *
 * Writers: POST /api/v1/values (anything on the network) and the feeds
 * (weather, aircraft). Readers: scene_engine.c. The engine re-sends a frame
 * when a value it shows changes, so pushed values get the same "onChange"
 * transitions as the clock. Values are not saved to flash. Thread-safe. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

#define VALUE_KEY_MAX    16     // including the NUL: keys are 1-15 of a-z 0-9 _
#define VALUE_TEXT_MAX   48     // including the NUL
#define VALUE_STORE_MAX  64     // number of keys

bool value_key_valid(const char *key);

/* Set (or replace) a value. ttl_s > 0: the value disappears after that many
 * seconds unless it is set again (stale data shows as "--", not as truth).
 * Returns false for a bad key or a full table. */
bool value_store_set(const char *key, const char *text, uint32_t ttl_s);
bool value_store_set_int(const char *key, long v, uint32_t ttl_s);

/* false when the key is missing or expired. */
bool value_store_get(const char *key, char *out, size_t cap);
bool value_store_get_number(const char *key, double *out);

void value_store_delete(const char *key);

/* {"cpu":"42","temp":"61"} (live keys only). Caller frees. */
cJSON *value_store_json(void);
