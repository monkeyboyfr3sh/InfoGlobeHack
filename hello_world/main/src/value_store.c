#include "value_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

typedef struct {
    char    key[VALUE_KEY_MAX];        // "" = free slot
    char    text[VALUE_TEXT_MAX];
    int64_t expires_us;                // 0 = never
} entry_t;

static entry_t           s_entries[VALUE_STORE_MAX];
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;

__attribute__((constructor)) static void store_lock_init(void) { s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf); }
static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

bool value_key_valid(const char *key)
{
    size_t n = key ? strlen(key) : 0;
    if (n == 0 || n >= VALUE_KEY_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

/* Call with the lock held. Frees expired entries as it goes. */
static entry_t *find(const char *key, entry_t **free_slot)
{
    int64_t now = esp_timer_get_time();
    entry_t *hit = NULL;
    for (int i = 0; i < VALUE_STORE_MAX; i++) {
        entry_t *e = &s_entries[i];
        if (e->key[0] && e->expires_us && now >= e->expires_us) e->key[0] = '\0';
        if (!e->key[0]) {
            if (free_slot && !*free_slot) *free_slot = e;
        } else if (!hit && !strcmp(e->key, key)) {
            hit = e;
        }
    }
    return hit;
}

bool value_store_set(const char *key, const char *text, uint32_t ttl_s)
{
    if (!value_key_valid(key) || !text) return false;
    lock();
    entry_t *spare = NULL;
    entry_t *e = find(key, &spare);
    if (!e) e = spare;
    if (e) {
        snprintf(e->key, sizeof(e->key), "%s", key);
        snprintf(e->text, sizeof(e->text), "%s", text);
        e->expires_us = ttl_s ? esp_timer_get_time() + (int64_t)ttl_s * 1000000 : 0;
    }
    unlock();
    return e != NULL;
}

bool value_store_set_int(const char *key, long v, uint32_t ttl_s)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", v);
    return value_store_set(key, buf, ttl_s);
}

bool value_store_get(const char *key, char *out, size_t cap)
{
    if (!key || !out || !cap) return false;
    lock();
    entry_t *e = find(key, NULL);
    if (e) snprintf(out, cap, "%s", e->text);
    unlock();
    return e != NULL;
}

bool value_store_get_number(const char *key, double *out)
{
    char buf[VALUE_TEXT_MAX];
    if (!value_store_get(key, buf, sizeof(buf))) return false;
    char *end;
    double v = strtod(buf, &end);
    if (end == buf) return false;
    *out = v;
    return true;
}

void value_store_delete(const char *key)
{
    if (!key) return;
    lock();
    entry_t *e = find(key, NULL);
    if (e) e->key[0] = '\0';
    unlock();
}

cJSON *value_store_json(void)
{
    cJSON *o = cJSON_CreateObject();
    lock();
    find("", NULL);                                 // drop expired entries
    for (int i = 0; i < VALUE_STORE_MAX; i++) {
        if (s_entries[i].key[0]) cJSON_AddStringToObject(o, s_entries[i].key, s_entries[i].text);
    }
    unlock();
    return o;
}
