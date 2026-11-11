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
#include "esp_random.h"
#include <math.h>

#include "clock_values.h"
#include "live_values.h"
#include "value_store.h"

#include "globe_frame.h"
#include "message_type.h"
#include "globe_anim.h"
#include "ir_led_task.h"
#include "esp_heap_caps.h"

static const char *TAG = "SCENES";

#define DIR_PATH       "/notes"
#define PLAYLIST_PATH  DIR_PATH "/playlist.json"
#define TEXT_TMPL_MAX  96

/* ------------------------------------------------------------------ */
/*  Live values                                                         */
/* ------------------------------------------------------------------ */

/* The substitution itself is pure C in live_values.c and clock_values.c,
 * compiled and checked against the designer's preview on a PC (npm test in
 * web/). This part only hands it the globe's clock, IP address and value store. */

_Static_assert(LIVE_KEY_MAX == VALUE_KEY_MAX, "pushed value names: live_values.h and value_store.h disagree");

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

/* Colon blink of the frame being rendered (set by frame_subst). Period is the
 * full on+off cycle in ms; 0 = no blink. Without a frame setting, {clock}
 * keeps its classic one-second blink. */
static bool     s_colon_set;
static uint16_t s_colon_period;
static char     s_colon_look = ' ';

/* One clock reading per template, so "{time}:{sec}" can't straddle a second.
 * newlib applies the TZ rule; the pure code only needs the resulting offset. */
static void subst(const char *tmpl, char *out, size_t cap, bool *live, bool steady)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm lt;
    localtime_r(&now, &lt);
    clock_now_t c = { .steady = steady, .colon_set = s_colon_set, .colon_period = s_colon_period, .colon_look = s_colon_look };
    clock_now_set(&c, now, (int32_t)tv.tv_usec, (int32_t)(clock_tm_seconds(&lt) - now));
    live_env_t env = { .now = &c, .get = value_store_get, .ip = ip_string };
    live_subst(tmpl, &env, out, cap, live);
}

void scene_subst(const char *tmpl, char *out, size_t cap, bool *live) { subst(tmpl, out, cap, live, false); }

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

#define SETTINGS_PATH DIR_PATH "/settings.json"

cJSON *settings_load(void)
{
    cJSON *j = read_json(SETTINGS_PATH, 4096);
    if (!cJSON_IsObject(j)) { cJSON_Delete(j); j = cJSON_CreateObject(); }
    return j;
}

static void settings_apply(const cJSON *s)
{
    cJSON *lat = cJSON_GetObjectItem(s, "lat"), *lon = cJSON_GetObjectItem(s, "lon");
    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) clock_values_set_location(lat->valuedouble, lon->valuedouble);
}

bool settings_save(const cJSON *patch)
{
    cJSON *s = settings_load();
    const cJSON *it;
    cJSON_ArrayForEach(it, patch) {
        cJSON_DeleteItemFromObject(s, it->string);
        cJSON_AddItemToObject(s, it->string, cJSON_Duplicate(it, true));
    }
    bool ok = write_json(SETTINGS_PATH, s);
    settings_apply(s);
    cJSON_Delete(s);
    return ok;
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
    if (ok) scene_engine_refresh();
    return ok;
}

/* ------------------------------------------------------------------ */
/*  Engine                                                              */
/* ------------------------------------------------------------------ */

enum { MOTION_NONE = 0, MOTION_PENDULUM, MOTION_DRIFT };
#define ON_CHANGE_NONE   (-1)
#define ON_CHANGE_RANDOM (-2)
#define ON_CHANGE_ANIM   (-3)       // a finite custom animation (change_anim) into the new text
#define ENG_SEQ_POOL     4096       // bytes for all "sequence" texts of one scene

typedef struct {
    char          text[TEXT_TMPL_MAX];
    globe_style_t style;
    uint32_t      ms;               // 0 = until the playlist moves on
    int           on_change;        // transition when a live value changes (flip clock), -1 none, -2 random, -3 anim
    uint8_t       motion;           // still text that swings (pendulum) or drifts left to right
    uint16_t      motion_ms;        // time per one-column step
    bool          colon_set;        // "blinkColon": {"period": ms, "look": "hide"|"dot"}
    uint16_t      colon_period;
    char          colon_look;
    ga_anim_t     anim;             // "anim": custom animation (docs/animations.md), GA_NONE = none
    ga_anim_t     change_anim;      // "onChange": "decode" or an anim object
} eng_frame_t;

/* Substitute a template (the frame's text, or a sequence step) with the frame's own colon blink. */
static void frame_subst_text(const eng_frame_t *fr, const char *tmpl, char *out, size_t cap, bool *live, bool steady)
{
    s_colon_set = fr->colon_set;
    s_colon_period = fr->colon_period;
    s_colon_look = fr->colon_look ? fr->colon_look : ' ';
    subst(tmpl, out, cap, live, steady);
    s_colon_set = false;
}

static void frame_subst(const eng_frame_t *fr, char *out, size_t cap, bool *live, bool steady)
{
    frame_subst_text(fr, fr->text, out, cap, live, steady);
}

typedef struct {
    char        id[SCENE_ID_MAX];
    char        name[40];
    eng_frame_t frames[SCENE_MAX_FRAMES];
    int         n;
    size_t      seq_used;
    char        seq_pool[ENG_SEQ_POOL];     // sequence texts, NUL-separated (ga_anim_t.texts points here)
} eng_scene_t;

typedef struct {
    char     scene[SCENE_ID_MAX];
    uint32_t seconds;               // 0 = forever (only sensible for a single item)
} eng_item_t;

static SemaphoreHandle_t s_lock;
static volatile bool     s_reload = true;
static volatile bool     s_refresh = false;      // re-read the playlist and settings without restarting
static volatile bool     s_next = false;
static volatile bool     s_cancel_override = false;
static eng_item_t        s_items[PLAYLIST_MAX];
static int               s_nitems;
static eng_scene_t      *s_scene;          // current scene (heap; large)
static char              s_chime_scene[SCENE_ID_MAX];
static int               s_chime_every;    // minutes (60, 30, 15), 0 = off
static int               s_chime_last = -1;

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
    // live values / motion
    char       shown_steady[TEXT_TMPL_MAX]; // shown text without blink: detects real changes
    TickType_t busy_until;                  // a change transition is playing
    TickType_t motion_t;
    uint32_t   motion_k;
    // custom animation (globe_anim): one still frame per step, on a clock
    bool       anim_on;
    bool       anim_settle;                 // the last step is up: the frame's own style follows when due
    ga_anim_t  anim;                        // what plays: the frame's anim, or its onChange one
    uint32_t   anim_k;
    TickType_t anim_due;
    char       anim_from[TEXT_TMPL_MAX];    // the text before a change (count)
} E;

static uint16_t s_anim_min_ms;              // settings "animMinMs": floor for every step, 0 = airtime only

void scene_engine_reload(void) { s_reload = true; }
void scene_engine_refresh(void) { s_refresh = true; }

static StaticSemaphore_t s_lock_buf;
__attribute__((constructor)) static void engine_lock_init(void) { s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf); }
static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

/* "anim": {"type":"decode", ...} -> *a (options validated by globe_anim). Sequence
 * texts go into the scene's pool; once it is full the rest are dropped.
 * Returns false (and GA_NONE) when there is no valid animation. */
static bool parse_anim(const cJSON *j, ga_anim_t *a, eng_scene_t *sc)
{
    memset(a, 0, sizeof(*a));
    if (!cJSON_IsObject(j)) return false;
    const cJSON *t = cJSON_GetObjectItem(j, "type");
    if (!cJSON_IsString(t) || !ga_init(a, t->valuestring)) return false;
    const cJSON *it;
    cJSON_ArrayForEach(it, j) {
        if (cJSON_IsNumber(it)) ga_set_num(a, it->string, it->valuedouble);
        else if (cJSON_IsBool(it)) ga_set_num(a, it->string, cJSON_IsTrue(it) ? 1 : 0);
        else if (cJSON_IsString(it)) { if (strcmp(it->string, "type")) ga_set_str(a, it->string, it->valuestring); }
        else if (cJSON_IsArray(it) && !strcmp(it->string, "texts")) {
            const cJSON *s;
            cJSON_ArrayForEach(s, it) {
                if (!cJSON_IsString(s) || a->ntexts >= GA_SEQ_MAX) continue;
                size_t n = ga_add_text(a, sc->seq_pool + sc->seq_used, sizeof(sc->seq_pool) - sc->seq_used, s->valuestring);
                if (!n) { ESP_LOGW(TAG, "Scene '%s': sequence texts over %d bytes, the rest dropped", sc->id, ENG_SEQ_POOL); break; }
                sc->seq_used += n;
            }
        }
    }
    return true;
}

static void parse_frame(cJSON *f, eng_frame_t *out, eng_scene_t *sc)
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
    if (parse_anim(cJSON_GetObjectItem(f, "anim"), &out->anim, sc)) out->style.entrance = -1;   // the animation is the entrance
    out->on_change = ON_CHANGE_NONE;
    cJSON *oc = cJSON_GetObjectItem(f, "onChange");
    if (cJSON_IsNumber(oc) && oc->valuedouble >= 0 && oc->valuedouble < g_globe_effect_count) out->on_change = (int)oc->valuedouble;
    else if (cJSON_IsString(oc) && !strcmp(oc->valuestring, "random")) out->on_change = ON_CHANGE_RANDOM;
    else if (cJSON_IsString(oc) || cJSON_IsObject(oc)) {        // a finite animation: "decode", {"type":"count",...}
        ga_anim_t *ca = &out->change_anim;
        if (cJSON_IsObject(oc)) parse_anim(oc, ca, sc);
        else if (out->anim.type && !strcmp(oc->valuestring, ga_type_name(out->anim.type))) *ca = out->anim;   // with the frame's options
        else ga_init(ca, oc->valuestring);
        if (ca->type && ga_finite(ca)) out->on_change = ON_CHANGE_ANIM;
        else ca->type = GA_NONE;
    }
    cJSON *mo = cJSON_GetObjectItem(f, "motion");
    if (cJSON_IsString(mo)) {
        if (!strcmp(mo->valuestring, "pendulum")) out->motion = MOTION_PENDULUM;
        else if (!strcmp(mo->valuestring, "drift")) out->motion = MOTION_DRIFT;
    }
    cJSON *bc = cJSON_GetObjectItem(f, "blinkColon");
    if (cJSON_IsObject(bc)) {
        cJSON *per = cJSON_GetObjectItem(bc, "period"), *look = cJSON_GetObjectItem(bc, "look");
        out->colon_set = true;
        out->colon_period = cJSON_IsNumber(per) && per->valuedouble >= 200 ? (uint16_t)per->valuedouble : 0;
        out->colon_look = cJSON_IsString(look) && !strcmp(look->valuestring, "dot") ? '.' : ' ';
    }
    cJSON *ms = cJSON_GetObjectItem(f, "motionMs");
    out->motion_ms = cJSON_IsNumber(ms) && ms->valuedouble >= 100 ? (uint16_t)ms->valuedouble : (out->motion == MOTION_DRIFT ? 160 : 250);
}

static bool load_scene(const char *id)
{
    cJSON *j = scene_load(id);
    cJSON *frames = j ? cJSON_GetObjectItem(j, "frames") : NULL;
    if (!cJSON_IsArray(frames) || cJSON_GetArraySize(frames) == 0) {
        cJSON_Delete(j);
        return false;
    }
    if (!s_scene) {   // ~10 KB with the sequence pool: PSRAM if there is any, speed doesn't matter here
        s_scene = heap_caps_calloc(1, sizeof(eng_scene_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_scene) s_scene = calloc(1, sizeof(eng_scene_t));
    }
    if (!s_scene) { cJSON_Delete(j); return false; }
    memset(s_scene, 0, sizeof(*s_scene));
    snprintf(s_scene->id, sizeof(s_scene->id), "%s", id);
    cJSON *name = cJSON_GetObjectItem(j, "name");
    snprintf(s_scene->name, sizeof(s_scene->name), "%s", cJSON_IsString(name) ? name->valuestring : id);
    cJSON *f;
    cJSON_ArrayForEach(f, frames) {
        if (s_scene->n >= SCENE_MAX_FRAMES) break;
        parse_frame(f, &s_scene->frames[s_scene->n++], s_scene);
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
    cJSON *ch = cJSON_GetObjectItem(p, "chime");
    cJSON *cs = ch ? cJSON_GetObjectItem(ch, "scene") : NULL, *ce = ch ? cJSON_GetObjectItem(ch, "every") : NULL;
    s_chime_every = 0;
    if (cJSON_IsString(cs) && scene_id_valid(cs->valuestring) && cJSON_IsNumber(ce) && ce->valuedouble >= 1) {
        snprintf(s_chime_scene, sizeof(s_chime_scene), "%s", cs->valuestring);
        s_chime_every = (int)ce->valuedouble;
    }
    cJSON_Delete(p);
    cJSON *st = settings_load();
    settings_apply(st);
    cJSON *am = cJSON_GetObjectItem(st, "animMinMs");   // tune animation pacing on the device without editing scenes
    s_anim_min_ms = cJSON_IsNumber(am) && am->valuedouble > 0 ? (uint16_t)(am->valuedouble < 5000 ? am->valuedouble : 5000) : 0;
    cJSON_Delete(st);
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

/* ms -> ticks, rounded up: an animation step must never come round sooner than its airtime. */
static TickType_t ms_ticks(uint32_t ms) { return (TickType_t)((ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS); }

/* Copy UTF-8 text, cut on a character boundary if it doesn't fit. */
static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Play an animation on the current frame from the next tick: the frame's own
 * (its entrance, or a loop) or its onChange one, `from` the text that was up. */
static void anim_start(const ga_anim_t *a, const char *from)
{
    E.anim = *a;
    E.anim_k = 0;
    E.anim_due = xTaskGetTickCount();
    E.anim_on = true;
    E.anim_settle = false;
    copy_text(E.anim_from, sizeof(E.anim_from), from ? from : "");
}

/* Compile and send the current frame; `steady` skips the entrance (value refresh). */
static void show_frame(QueueHandle_t q, bool steady)
{
    const eng_frame_t *fr = &s_scene->frames[E.frame];
    char text[TEXT_TMPL_MAX];
    frame_subst(fr, text, sizeof(text), &E.live, false);
    if (!steady) {
        E.anim_on = E.anim_settle = false;          // a new frame ends any animation...
        if (fr->anim.type) {                        // ...or starts its own instead of an entrance
            anim_start(&fr->anim, NULL);
            frame_subst(fr, E.shown_steady, sizeof(E.shown_steady), NULL, true);
            E.second_pending = false;
            return;
        }
    }
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
    frame_subst(fr, E.shown_steady, sizeof(E.shown_steady), NULL, true);
    E.second_pending = n2 > 0;
    if (n2) E.second_at = xTaskGetTickCount() + pdMS_TO_TICKS(g_globe_effects[st.entrance].duration_ms);
}

static int random_effect(void)
{
    for (;;) {
        int id = esp_random() % g_globe_effect_count;
        if (g_globe_effects[id].same_as < 0) return id;
    }
}

/* A live value changed (e.g. the minute): play the frame's change transition
 * into the new text, then settle into its still/spin style (flip clock). */
static void change_frame(QueueHandle_t q, const eng_frame_t *fr)
{
    char text[TEXT_TMPL_MAX];
    frame_subst(fr, text, sizeof(text), NULL, false);
    uint8_t enc[GLOBE_TEXT_MAX + 8];
    size_t len = globe_text_encode(text, enc, GLOBE_TEXT_MAX, NULL);
    globe_style_t st = fr->style;
    st.entrance = fr->on_change == ON_CHANGE_RANDOM ? random_effect() : fr->on_change;
    uint8_t f1[GLOBE_FRAME_MAX], f2[GLOBE_FRAME_MAX];
    size_t n1 = 0, n2 = 0;
    if (!globe_frame_compile(enc, len, &st, f1, &n1, f2, &n2)) return;
    send_bytes(q, f1, n1);
    lock();
    snprintf(E.shown, sizeof(E.shown), "%s", text);
    unlock();
    frame_subst(fr, E.shown_steady, sizeof(E.shown_steady), NULL, true);
    TickType_t done = xTaskGetTickCount() + pdMS_TO_TICKS(g_globe_effects[st.entrance].duration_ms);
    E.busy_until = done;
    E.second_pending = n2 > 0;
    if (n2) E.second_at = done;
}

/* Still text that moves by re-sending it at a new position (shift byte):
 * a pendulum swing, or a left-to-right drift (the globe's own spin only
 * goes right to left). */
static void motion_step(QueueHandle_t q, const eng_frame_t *fr, TickType_t now)
{
    if (now - E.motion_t < pdMS_TO_TICKS(fr->motion_ms)) return;
    E.motion_t = now;
    char text[TEXT_TMPL_MAX];
    frame_subst(fr, text, sizeof(text), NULL, false);
    uint8_t enc[GLOBE_TEXT_MAX + 8];
    size_t len = globe_text_encode(text, enc, GLOBE_TEXT_MAX, NULL);
    int span = (int)len * 2 + 1;              // shift 0 (right) .. 2*len (left)
    int shift;
    if (fr->motion == MOTION_PENDULUM) {
        double ph = 2 * M_PI * (double)E.motion_k / (span * 2);
        shift = (int)len + (int)lround(len * sin(ph));
    } else {
        shift = (span - 1) - (int)(E.motion_k % span);   // one column right per step, then wrap
    }
    E.motion_k++;
    globe_style_t st = fr->style;
    st.entrance = -1;
    if (st.after != GLOBE_AFTER_FLASH) st.after = GLOBE_AFTER_HOLD;
    st.shift = shift < 0 ? 0 : shift;
    uint8_t f1[GLOBE_FRAME_MAX], f2[GLOBE_FRAME_MAX];
    size_t n1 = 0, n2 = 0;
    if (!globe_frame_compile(enc, len, &st, f1, &n1, f2, &n2)) return;
    send_bytes(q, f1, n1);
    lock();
    snprintf(E.shown, sizeof(E.shown), "%s", text);
    unlock();
    frame_subst(fr, E.shown_steady, sizeof(E.shown_steady), NULL, true);
}

/* One custom animation step, when it is due: still text (05) at the step's
 * position. Steps run on a clock (E.anim_due) and each stays up for at least
 * its frame's airtime, so the display queue never backs up and this never
 * blocks; more than a whole step behind (a slow tick, a blocked send), steps
 * are dropped rather than sent in a burst. After a finite animation's last
 * step has had its time, the frame settles into its own style. */
static void anim_tick(QueueHandle_t q, const eng_frame_t *fr, TickType_t now)
{
    if ((int32_t)(now - E.anim_due) < 0) return;
    if (E.anim_settle) {
        E.anim_on = E.anim_settle = false;
        show_frame(q, true);                        // spin / hold / flash, with live values
        return;
    }
    char target[TEXT_TMPL_MAX], text[TEXT_TMPL_MAX];
    frame_subst(fr, target, sizeof(target), NULL, false);   // live values stay live during the animation
    uint32_t gap = ir_get_frame_gap_ms();
    ga_input_t in = {
        .target = target, .from = E.anim_from,
        .shift = fr->style.after == GLOBE_AFTER_HOLD ? fr->style.shift : -1,
        .gap_ms = (uint16_t)gap, .min_ms = s_anim_min_ms,
    };
    ga_step_t st;
    ga_step(&E.anim, &in, E.anim_k, &st);
    for (int skip = 0; skip < 16 && !st.done && (int32_t)(now - (E.anim_due + ms_ticks(st.ms))) >= 0; skip++) {
        E.anim_due += ms_ticks(st.ms);
        ga_step(&E.anim, &in, ++E.anim_k, &st);
    }
    if (st.tmpl) frame_subst_text(fr, st.text, text, sizeof(text), NULL, false);   // a sequence text
    else copy_text(text, sizeof(text), st.text);

    uint8_t enc[GLOBE_TEXT_MAX + 8];
    size_t len = globe_text_encode(text, enc, GLOBE_TEXT_MAX, NULL);
    bool loops = !ga_finite(&E.anim);
    globe_style_t s = { .entrance = -1, .after = GLOBE_AFTER_HOLD, .blink_chars = 0, .shift = in.shift };
    if (st.shift >= 0) s.shift = st.shift;
    if (st.blink >= 0) s.blink_chars = st.blink;
    else if (loops && fr->style.after == GLOBE_AFTER_FLASH) s.after = GLOBE_AFTER_FLASH;      // a loop keeps the frame's blinking
    else if (loops && fr->style.after == GLOBE_AFTER_HOLD) s.blink_chars = fr->style.blink_chars;
    uint8_t f1[GLOBE_FRAME_MAX], f2[GLOBE_FRAME_MAX];
    size_t n1 = 0, n2 = 0;
    if (globe_frame_compile(enc, len, &s, f1, &n1, f2, &n2)) send_bytes(q, f1, n1);
    lock();
    copy_text(E.shown, sizeof(E.shown), text);
    unlock();

    uint32_t hold = st.ms, air = (uint32_t)n1 * 8 + gap;    // the real frame, after live values
    if (hold < air) hold = air;
    E.anim_due += ms_ticks(hold);
    TickType_t sent = xTaskGetTickCount();
    if ((int32_t)(E.anim_due - sent) <= 0) E.anim_due = sent + ms_ticks(hold);   // still behind: start the clock again
    E.anim_k++;
    if (st.done) E.anim_settle = true;
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
    E.busy_until = E.item_t0;
    E.motion_k = 0;
    E.anim_on = E.anim_settle = false;
    if (s_scene && s_scene->n) show_frame(q, false);
}

static void pending_second(QueueHandle_t q)
{
    if (!E.second_pending || (int32_t)(xTaskGetTickCount() - E.second_at) < 0) return;
    E.second_pending = false;
    // the frame's still part (hold/flash) after its entrance
    const eng_frame_t *fr = &s_scene->frames[E.frame];
    char text[TEXT_TMPL_MAX];
    frame_subst(fr, text, sizeof(text), NULL, false);
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
        s_refresh = false;
    }
    if (s_refresh) {
        // The playlist or the settings were saved (a time changed, something was
        // switched on or off). Carry on with what is showing if it is still in the
        // list, so adjusting a time on the web page doesn't restart the display.
        s_refresh = false;
        char cur[SCENE_ID_MAX] = "";
        if (E.item < s_nitems) snprintf(cur, sizeof(cur), "%s", s_items[E.item].scene);
        lock();
        load_playlist();
        unlock();
        int k = -1;
        for (int i = 0; i < s_nitems; i++) if (!strcmp(s_items[i].scene, cur)) { k = i; break; }
        if (k >= 0) E.item = k;
        else {
            if (E.item >= s_nitems) E.item = 0;
            if (!E.override) E.was_active = false;  // what was showing left the list: move on now
        }
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

    if (s_cancel_override) {                        // "Resume": straight back to the playlist
        s_cancel_override = false;
        if (E.override) { E.override = false; start_item(q); return; }
    }

    if (s_next) {                                   // "Next" from the dashboard
        s_next = false;
        if (E.override) E.override = false;
        else E.item = (E.item + 1) % s_nitems;
        start_item(q);
        return;
    }

    // Chime: play the chime scene once on the hour (or every 30/15 min).
    if (s_chime_every && !E.override) {
        time_t tnow = time(NULL);
        struct tm lt;
        localtime_r(&tnow, &lt);
        int key = lt.tm_yday * 1440 + lt.tm_hour * 60 + lt.tm_min;
        if (lt.tm_year > 120 && lt.tm_min % s_chime_every == 0 && lt.tm_sec < 3 && key != s_chime_last) {
            s_chime_last = key;
            if (scene_engine_play(s_chime_scene, 0)) return;
        }
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

    if (E.anim_on) {                                // a custom animation has the frame for now
        anim_tick(q, fr, now);
        vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }

    // Live values and motion (not while an entrance or change transition plays).
    uint32_t ent = fr->style.entrance >= 0 ? g_globe_effects[fr->style.entrance].duration_ms : 0;
    bool animating = E.second_pending || now - E.frame_t0 < pdMS_TO_TICKS(ent) || (int32_t)(E.busy_until - now) > 0;
    if (!animating) {
        if (E.live) {
            char steady[TEXT_TMPL_MAX];
            frame_subst(fr, steady, sizeof(steady), NULL, true);
            if (strcmp(steady, E.shown_steady)) {           // a real change (minute, date, ...)
                if (fr->on_change == ON_CHANGE_ANIM) {      // e.g. decode into the new minute
                    anim_start(&fr->change_anim, E.shown_steady);
                    copy_text(E.shown_steady, sizeof(E.shown_steady), steady);
                }
                else if (fr->on_change != ON_CHANGE_NONE) change_frame(q, fr);
                else if (!fr->motion) show_frame(q, true);
                else frame_subst(fr, E.shown_steady, sizeof(E.shown_steady), NULL, true);
            } else if (!fr->motion) {                       // only the colon blinked
                char text[TEXT_TMPL_MAX];
                frame_subst(fr, text, sizeof(text), NULL, false);
                if (strcmp(text, E.shown)) show_frame(q, true);
            }
        }
        if (fr->motion && fr->style.after != GLOBE_AFTER_SPIN && !E.anim_on) motion_step(q, fr, now);
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
void scene_engine_resume(void) { s_cancel_override = true; }

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
