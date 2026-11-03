#include "scene_engine.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "globe_frame.h"
#include "message_type.h"

static const char *TAG = "SCENES";

#define DIR_PATH       "/notes"
#define PLAYLIST_PATH  DIR_PATH "/playlist.json"
#define TEXT_TMPL_MAX  96

/* ------------------------------------------------------------------ */
/*  Live values                                                         */
/* ------------------------------------------------------------------ */

static void ip_string(char *buf, size_t len)
{
    const char *keys[] = { "ETH_DEF", "WIFI_STA_DEF" };
    buf[0] = '\0';
    for (int i = 0; i < 2; i++) {
        esp_netif_t *n = esp_netif_get_handle_from_ifkey(keys[i]);
        esp_netif_ip_info_t ip;
        if (n && esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.ip.addr) {
            snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
            return;
        }
    }
}

/* One value; returns false for an unknown name (left in the text as typed). */
static bool value_of(const char *name, char *out, size_t cap, bool *live)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm t;
    localtime_r(&now, &t);
    const char *fmt = NULL;

    if (!strcmp(name, "clock")) {             // the classic clock: "07:05 PM", colon blinks
        strftime(out, cap, "%I:%M %p", &t);
        if (tv.tv_usec >= 500000 && cap > 2) out[2] = ' ';
        *live = true;
        return true;
    }
    if (!strcmp(name, "time")) {              // "7:05"
        int h = t.tm_hour % 12 ? t.tm_hour % 12 : 12;
        snprintf(out, cap, "%d:%02d", h, t.tm_min);
        *live = true;
        return true;
    }
    if      (!strcmp(name, "time24")) fmt = "%H:%M";
    else if (!strcmp(name, "ampm"))   fmt = "%p";
    else if (!strcmp(name, "sec"))    fmt = "%S";
    else if (!strcmp(name, "date"))   fmt = "%a %d %b";   // "Mon 02 Nov"
    else if (!strcmp(name, "day"))    fmt = "%A";
    else if (!strcmp(name, "dd"))     fmt = "%d";
    else if (!strcmp(name, "month"))  fmt = "%B";
    else if (!strcmp(name, "mon"))    fmt = "%b";
    else if (!strcmp(name, "year"))   fmt = "%Y";
    if (fmt) {
        strftime(out, cap, fmt, &t);
        *live = true;
        return true;
    }
    if (!strcmp(name, "ip")) {
        ip_string(out, cap);
        *live = true;
        return true;
    }
    return false;
}

void scene_subst(const char *tmpl, char *out, size_t cap, bool *live)
{
    size_t o = 0;
    bool lv = false;
    for (const char *p = tmpl; *p && o + 1 < cap;) {
        const char *close = (*p == '{') ? strchr(p, '}') : NULL;
        if (close && close - p > 1 && close - p < 16) {
            char name[16];
            memcpy(name, p + 1, close - p - 1);
            name[close - p - 1] = '\0';
            char val[48];
            if (value_of(name, val, sizeof(val), &lv)) {
                for (const char *v = val; *v && o + 1 < cap;) out[o++] = *v++;
                p = close + 1;
                continue;
            }
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
    if (live) *live = lv;
}

/* ------------------------------------------------------------------ */
/*  Storage                                                             */
/* ------------------------------------------------------------------ */

static const char *BUILTIN_CLOCK =
    "{\"id\":\"clock\",\"name\":\"Clock\",\"builtin\":true,\"frames\":[{\"text\":\"{clock}\","
    "\"entrance\":null,\"after\":\"hold\",\"blink\":0,\"shift\":-1,\"seconds\":0}]}";

bool scene_id_valid(const char *id)
{
    size_t n = id ? strlen(id) : 0;
    if (n == 0 || n >= SCENE_ID_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        if (!(islower((unsigned char)id[i]) || isdigit((unsigned char)id[i]) || id[i] == '-')) return false;
    }
    return true;
}

static void scene_path(const char *id, char *buf, size_t len)
{
    snprintf(buf, len, DIR_PATH "/sc_%s.json", id);
}

static cJSON *read_json(const char *path, size_t max)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    cJSON *j = NULL;
    if (n > 0 && (size_t)n <= max) {
        char *buf = malloc(n + 1);
        if (buf && fread(buf, 1, n, f) == (size_t)n) {
            buf[n] = '\0';
            j = cJSON_Parse(buf);
        }
        free(buf);
    }
    fclose(f);
    return j;
}

static bool write_json(const char *path, const cJSON *j)
{
    char *out = cJSON_PrintUnformatted(j);
    if (!out) return false;
    char tmp[64];
    snprintf(tmp, sizeof(tmp), DIR_PATH "/tmp.json");
    bool ok = false;
    FILE *f = fopen(tmp, "w");
    if (f) {
        ok = fputs(out, f) >= 0;
        ok = (fclose(f) == 0) && ok;
    }
    if (ok) {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) ESP_LOGE(TAG, "Could not write %s", path);
    free(out);
    return ok;
}

cJSON *scene_load(const char *id)
{
    if (!scene_id_valid(id)) return NULL;
    char path[64];
    scene_path(id, path, sizeof(path));
    cJSON *j = read_json(path, 16 * 1024);
    if (!j && !strcmp(id, "clock")) j = cJSON_Parse(BUILTIN_CLOCK);
    return j;
}

bool scene_save(const char *id, const cJSON *scene)
{
    if (!scene_id_valid(id)) return false;
    char path[64];
    scene_path(id, path, sizeof(path));
    bool ok = write_json(path, scene);
    if (ok) scene_engine_reload();
    return ok;
}

bool scene_delete(const char *id)
{
    if (!scene_id_valid(id)) return false;
    char path[64];
    scene_path(id, path, sizeof(path));
    bool ok = remove(path) == 0;
    scene_engine_reload();
    return ok;
}

cJSON *scene_list(void)
{
    cJSON *arr = cJSON_CreateArray();
    bool have_clock = false;
    DIR *d = opendir(DIR_PATH);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            const char *n = e->d_name;
            size_t len = strlen(n);
            if (strncmp(n, "sc_", 3) || len < 9 || strcmp(n + len - 5, ".json")) continue;
            char id[SCENE_ID_MAX];
            size_t idlen = len - 8;
            if (idlen >= sizeof(id)) continue;
            memcpy(id, n + 3, idlen);
            id[idlen] = '\0';
            cJSON *s = scene_load(id);
            if (!s) continue;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "id", id);
            cJSON *name = cJSON_GetObjectItem(s, "name");
            cJSON_AddStringToObject(o, "name", cJSON_IsString(name) ? name->valuestring : id);
            cJSON_AddNumberToObject(o, "frames", cJSON_GetArraySize(cJSON_GetObjectItem(s, "frames")));
            cJSON_AddItemToArray(arr, o);
            cJSON_Delete(s);
            if (!strcmp(id, "clock")) have_clock = true;
        }
        closedir(d);
    }
    if (!have_clock) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", "clock");
        cJSON_AddStringToObject(o, "name", "Clock");
        cJSON_AddNumberToObject(o, "frames", 1);
        cJSON_AddBoolToObject(o, "builtin", true);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

cJSON *playlist_load(void)
{
    cJSON *j = read_json(PLAYLIST_PATH, 8 * 1024);
    if (!cJSON_IsObject(j) || !cJSON_IsArray(cJSON_GetObjectItem(j, "items"))) {
        cJSON_Delete(j);
        j = cJSON_Parse("{\"items\":[{\"scene\":\"clock\",\"seconds\":0}]}");
    }
    return j;
}

bool playlist_save(const cJSON *playlist)
{
    bool ok = write_json(PLAYLIST_PATH, playlist);
    if (ok) scene_engine_reload();
    return ok;
}

/* ------------------------------------------------------------------ */
/*  Engine                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char          text[TEXT_TMPL_MAX];
    globe_style_t style;
    uint32_t      ms;               // 0 = until the playlist moves on
} eng_frame_t;

typedef struct {
    char        id[SCENE_ID_MAX];
    char        name[40];
    eng_frame_t frames[SCENE_MAX_FRAMES];
    int         n;
} eng_scene_t;

typedef struct {
    char     scene[SCENE_ID_MAX];
    uint32_t seconds;               // 0 = forever (only sensible for a single item)
} eng_item_t;

static SemaphoreHandle_t s_lock;
static volatile bool     s_reload = true;
static volatile bool     s_next = false;
static eng_item_t        s_items[PLAYLIST_MAX];
static int               s_nitems;
static eng_scene_t      *s_scene;          // current scene (heap; large)

static struct {
    bool       was_active;
    int        item;
    int        frame;
    TickType_t item_t0, frame_t0;
    char       shown[TEXT_TMPL_MAX];        // text currently on the globe (substituted)
    bool       live;
    bool       second_pending;
    TickType_t second_at;
    // "play now" override
    bool       override;
    char       ov_scene[SCENE_ID_MAX];
    uint32_t   ov_seconds;
    TickType_t ov_t0;
    // manual text shown while the engine is paused
    char       manual[TEXT_TMPL_MAX];
} E;

void scene_engine_reload(void) { s_reload = true; }

static StaticSemaphore_t s_lock_buf;
__attribute__((constructor)) static void engine_lock_init(void) { s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf); }
static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static void parse_frame(cJSON *f, eng_frame_t *out)
{
    memset(out, 0, sizeof(*out));
    out->style.entrance = -1;
    out->style.after = GLOBE_AFTER_SPIN;
    out->style.shift = -1;
    cJSON *t = cJSON_GetObjectItem(f, "text");
    if (cJSON_IsString(t)) snprintf(out->text, sizeof(out->text), "%s", t->valuestring);
    cJSON *e = cJSON_GetObjectItem(f, "entrance");
    if (cJSON_IsNumber(e) && e->valuedouble >= 0 && e->valuedouble < g_globe_effect_count) out->style.entrance = (int)e->valuedouble;
    else if (cJSON_IsString(e)) out->style.entrance = globe_effect_lookup(e->valuestring);
    cJSON *a = cJSON_GetObjectItem(f, "after");
    if (cJSON_IsString(a)) {
        if (!strcmp(a->valuestring, "hold"))         out->style.after = GLOBE_AFTER_HOLD;
        else if (!strcmp(a->valuestring, "flash"))   out->style.after = GLOBE_AFTER_FLASH;
        else if (!strcmp(a->valuestring, "instant")) out->style.after = GLOBE_AFTER_INSTANT;
    }
    cJSON *b = cJSON_GetObjectItem(f, "blink");
    if (cJSON_IsNumber(b)) out->style.blink_chars = (int)b->valuedouble;
    cJSON *s = cJSON_GetObjectItem(f, "shift");
    if (cJSON_IsNumber(s)) out->style.shift = (int)s->valuedouble;
    cJSON *sec = cJSON_GetObjectItem(f, "seconds");
    if (cJSON_IsNumber(sec) && sec->valuedouble > 0) out->ms = (uint32_t)(sec->valuedouble * 1000);
}

static bool load_scene(const char *id)
{
    cJSON *j = scene_load(id);
    cJSON *frames = j ? cJSON_GetObjectItem(j, "frames") : NULL;
    if (!cJSON_IsArray(frames) || cJSON_GetArraySize(frames) == 0) {
        cJSON_Delete(j);
        return false;
    }
    if (!s_scene) s_scene = calloc(1, sizeof(eng_scene_t));
    if (!s_scene) { cJSON_Delete(j); return false; }
    memset(s_scene, 0, sizeof(*s_scene));
    snprintf(s_scene->id, sizeof(s_scene->id), "%s", id);
    cJSON *name = cJSON_GetObjectItem(j, "name");
    snprintf(s_scene->name, sizeof(s_scene->name), "%s", cJSON_IsString(name) ? name->valuestring : id);
    cJSON *f;
    cJSON_ArrayForEach(f, frames) {
        if (s_scene->n >= SCENE_MAX_FRAMES) break;
        parse_frame(f, &s_scene->frames[s_scene->n++]);
    }
    cJSON_Delete(j);
    return true;
}

static void load_playlist(void)
{
    cJSON *p = playlist_load();
    s_nitems = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(p, "items")) {
        if (s_nitems >= PLAYLIST_MAX) break;
        cJSON *sc = cJSON_GetObjectItem(it, "scene");
        cJSON *se = cJSON_GetObjectItem(it, "seconds");
        if (!cJSON_IsString(sc) || !scene_id_valid(sc->valuestring)) continue;
        snprintf(s_items[s_nitems].scene, SCENE_ID_MAX, "%s", sc->valuestring);
        s_items[s_nitems].seconds = cJSON_IsNumber(se) && se->valuedouble > 0 ? (uint32_t)se->valuedouble : 0;
        s_nitems++;
    }
    cJSON_Delete(p);
    if (s_nitems == 0) {
        snprintf(s_items[0].scene, SCENE_ID_MAX, "clock");
        s_items[0].seconds = 0;
        s_nitems = 1;
    }
}

static void send_bytes(QueueHandle_t q, const uint8_t *b, size_t n)
{
    uint8_t *copy = malloc(n);
    if (!copy) return;
    memcpy(copy, b, n);
    push_buffer_to_queue(q, copy, n);
}

/* Compile and send the current frame; `steady` skips the entrance (value refresh). */
static void show_frame(QueueHandle_t q, bool steady)
{
    const eng_frame_t *fr = &s_scene->frames[E.frame];
    char text[TEXT_TMPL_MAX];
    scene_subst(fr->text, text, sizeof(text), &E.live);
    uint8_t enc[GLOBE_TEXT_MAX + 8];
    size_t len = globe_text_encode(text, enc, GLOBE_TEXT_MAX, NULL);
    globe_style_t st = fr->style;
    if (steady) st.entrance = -1;          // value refresh: no entrance again
    uint8_t f1[GLOBE_FRAME_MAX], f2[GLOBE_FRAME_MAX];
    size_t n1 = 0, n2 = 0;
    if (!globe_frame_compile(enc, len, &st, f1, &n1, f2, &n2)) return;
    send_bytes(q, f1, n1);
    lock();
    snprintf(E.shown, sizeof(E.shown), "%s", text);
    unlock();
    E.second_pending = n2 > 0;
    if (n2) E.second_at = xTaskGetTickCount() + pdMS_TO_TICKS(g_globe_effects[st.entrance].duration_ms);
}

/* Start the playlist item E.item (or the override scene) from its first frame. */
static void start_item(QueueHandle_t q)
{
    if (E.override && !load_scene(E.ov_scene)) E.override = false;
    if (!E.override) {
        int tries = 0;
        while (tries < s_nitems && !load_scene(s_items[E.item].scene)) {
            ESP_LOGW(TAG, "Scene '%s' missing or empty, skipping", s_items[E.item].scene);
            E.item = (E.item + 1) % s_nitems;
            tries++;
        }
        if (tries == s_nitems) load_scene("clock");
    }
    E.item_t0 = E.frame_t0 = xTaskGetTickCount();
    E.frame = 0;
    E.second_pending = false;
    if (s_scene && s_scene->n) show_frame(q, false);
}

static void pending_second(QueueHandle_t q)
{
    if (!E.second_pending || (int32_t)(xTaskGetTickCount() - E.second_at) < 0) return;
    E.second_pending = false;
    // the frame's still part (hold/flash) after its entrance
    const eng_frame_t *fr = &s_scene->frames[E.frame];
    char text[TEXT_TMPL_MAX];
    scene_subst(fr->text, text, sizeof(text), NULL);
    uint8_t enc[GLOBE_TEXT_MAX + 8];
    size_t len = globe_text_encode(text, enc, GLOBE_TEXT_MAX, NULL);
    globe_style_t st = fr->style;
    st.entrance = -1;
    uint8_t f1[GLOBE_FRAME_MAX], f2[GLOBE_FRAME_MAX];
    size_t n1, n2;
    if (globe_frame_compile(enc, len, &st, f1, &n1, f2, &n2)) send_bytes(q, f1, n1);
}

void scene_engine_tick(QueueHandle_t q, bool active)
{
    if (!active) {
        E.was_active = false;
        vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }
    TickType_t now = xTaskGetTickCount();
    if (s_reload) {
        s_reload = false;
        lock();
        load_playlist();
        unlock();
        if (E.item >= s_nitems) E.item = 0;
        E.was_active = false;                       // restart with the new content
    }
    if (!E.was_active) {
        E.was_active = true;
        lock();
        E.manual[0] = '\0';
        unlock();
        start_item(q);
        return;
    }
    if (!s_scene || !s_scene->n) { vTaskDelay(pdMS_TO_TICKS(10)); return; }

    if (s_next) {                                   // "Next" from the dashboard
        s_next = false;
        if (E.override) E.override = false;
        else E.item = (E.item + 1) % s_nitems;
        start_item(q);
        return;
    }

    // Override finished -> back to the playlist where it was.
    if (E.override) {
        uint32_t total = 0;
        for (int i = 0; i < s_scene->n; i++) total += s_scene->frames[i].ms ? s_scene->frames[i].ms : 5000;
        uint32_t limit = E.ov_seconds ? E.ov_seconds * 1000 : total;
        if (now - E.ov_t0 >= pdMS_TO_TICKS(limit)) {
            E.override = false;
            start_item(q);
            return;
        }
    } else if (s_nitems > 1 && s_items[E.item].seconds &&
               now - E.item_t0 >= pdMS_TO_TICKS(s_items[E.item].seconds * 1000)) {
        E.item = (E.item + 1) % s_nitems;
        start_item(q);
        return;
    }

    // Next frame within the scene.
    const eng_frame_t *fr = &s_scene->frames[E.frame];
    uint32_t fms = fr->ms ? fr->ms : (s_scene->n > 1 ? 5000 : 0);
    if (fms && now - E.frame_t0 >= pdMS_TO_TICKS(fms)) {
        E.frame = (E.frame + 1) % s_scene->n;
        E.frame_t0 = now;
        show_frame(q, false);
        return;
    }

    pending_second(q);

    // Live values: re-send the steady frame when the text changes
    // (not during an entrance animation).
    if (E.live && !E.second_pending) {
        uint32_t ent = fr->style.entrance >= 0 ? g_globe_effects[fr->style.entrance].duration_ms : 0;
        if (now - E.frame_t0 >= pdMS_TO_TICKS(ent)) {
            char text[TEXT_TMPL_MAX];
            scene_subst(fr->text, text, sizeof(text), NULL);
            if (strcmp(text, E.shown)) show_frame(q, true);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}

bool scene_engine_play(const char *id, unsigned seconds)
{
    cJSON *j = scene_load(id);
    if (!j) return false;
    cJSON_Delete(j);
    lock();
    snprintf(E.ov_scene, sizeof(E.ov_scene), "%s", id);
    E.ov_seconds = seconds;
    E.ov_t0 = xTaskGetTickCount();
    E.override = true;
    E.was_active = false;           // restart on the next tick with the override
    unlock();
    return true;
}

void scene_engine_next(void) { s_next = true; }

void scene_engine_note_manual(const char *text)
{
    lock();
    snprintf(E.manual, sizeof(E.manual), "%s", text ? text : "");
    unlock();
}

cJSON *scene_engine_status(void)
{
    extern volatile bool g_time_on;
    cJSON *o = cJSON_CreateObject();
    lock();
    bool running = g_time_on;
    cJSON_AddStringToObject(o, "mode", !running ? "manual" : E.override ? "scene" : "playlist");
    if (running && s_scene) {
        cJSON_AddStringToObject(o, "scene", s_scene->id);
        cJSON_AddStringToObject(o, "name", s_scene->name);
        cJSON_AddNumberToObject(o, "frame", E.frame);
        cJSON_AddNumberToObject(o, "frames", s_scene->n);
        cJSON_AddStringToObject(o, "text", E.shown);
        if (!E.override && s_nitems > 1) {
            cJSON_AddNumberToObject(o, "item", E.item);
            uint32_t sec = s_items[E.item].seconds;
            if (sec) {
                uint32_t el = (xTaskGetTickCount() - E.item_t0) * portTICK_PERIOD_MS / 1000;
                cJSON_AddNumberToObject(o, "itemRemaining", el < sec ? sec - el : 0);
                cJSON_AddNumberToObject(o, "itemSeconds", sec);
            }
        }
    } else if (!running) {
        cJSON_AddStringToObject(o, "text", E.manual);
    }
    unlock();
    return o;
}
