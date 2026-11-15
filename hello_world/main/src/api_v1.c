/* REST API v1: designed frames in, protocol bytes out (via globe_frame).
 *
 *   POST /api/v1/frame   {"text":"SERVER OK","entrance":"Pincer"|2|null,
 *                         "after":"spin|hold|flash|instant","blink":2,"shift":-1,
 *                         "keepClock":false}
 *        -> {"ok":true,"bytes":"00 00 ...","then":"05 ...","thenAfterMs":1500,"unsupported":0}
 *   POST /api/v1/clear   blank the display
 *   GET  /api/v1/effects the effect table (id, name, group, durationMs, sameAs)
 *   GET  /api/v1/auth    {"required":bool,"valid":bool,...}; POST {"token":".."} sets it ("" removes)
 *   GET  /api/v1/ws      WebSocket live mirror (live.c)
 *   GET  /designer       the Vite-built designer (designer.html.gz on SPIFFS)
 * Full list: docs/api.md. When an access token is set (auth.c), every POST and
 * DELETE here needs it; GETs stay open.
 *
 * All API responses allow cross-origin requests so the designer can be
 * developed on a PC (vite dev server) against the real globe. */

#include "api_v1.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "cJSON.h"

#include "globe_frame.h"
#include "message_type.h"
#include "frame_maker_task.h"
#include "scene_engine.h"
#include "value_store.h"
#include "feeds.h"
#include "auth.h"
#include "live.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"

static const char *TAG = "API_V1";

/* Showing something while the display is switched off (settings "display": false). */
#define DISPLAY_OFF_MSG "The display is switched off. Switch it on first."

static QueueHandle_t s_queue;

/* The follow-up frame (entrance, then hold/flash) waits here until the
 * entrance has played. A newer request replaces it. */
static SemaphoreHandle_t s_lock;
static TaskHandle_t      s_pending_task;
static uint8_t           s_pending[GLOBE_FRAME_MAX];
static size_t            s_pending_len;
static TickType_t        s_pending_due;

static void send_bytes(const uint8_t *b, size_t n)
{
    uint8_t *copy = malloc(n);
    if (!copy) return;
    memcpy(copy, b, n);
    push_buffer_to_queue(s_queue, copy, n);       // takes ownership
}

static void pending_task(void *arg)
{
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_pending_len) {
            TickType_t now = xTaskGetTickCount();
            wait = (int32_t)(s_pending_due - now) > 0 ? s_pending_due - now : 0;
        }
        xSemaphoreGive(s_lock);

        if (wait) {
            ulTaskNotifyTake(pdTRUE, wait);   // woken early when the request changes
            continue;
        }
        uint8_t buf[GLOBE_FRAME_MAX];
        size_t n;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        n = s_pending_len;
        memcpy(buf, s_pending, n);
        s_pending_len = 0;
        xSemaphoreGive(s_lock);
        if (n) send_bytes(buf, n);
    }
}

static void set_pending(const uint8_t *b, size_t n, int delay_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_pending_len = n;
    if (n) {
        memcpy(s_pending, b, n);
        s_pending_due = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    }
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_pending_task);
}

/* ---- helpers ---- */

static void cors(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", AUTH_CORS_HEADERS);
}

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *obj)
{
    char *out = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    cors(req);
    if (status) httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out ? out : "{}");
    free(out);
    return ESP_OK;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", false);
    cJSON_AddStringToObject(o, "error", msg);
    return send_json(req, status, o);
}

static cJSON *recv_body_max(httpd_req_t *req, size_t max)
{
    if (req->content_len == 0 || req->content_len > max) return NULL;
    char *buf = malloc(req->content_len + 1);
    if (!buf) return NULL;
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { free(buf); return NULL; }
        got += r;
    }
    buf[got] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    return root;
}

static cJSON *recv_body(httpd_req_t *req) { return recv_body_max(req, 2048); }

static void hex_of(const uint8_t *b, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    out[0] = '\0';
    for (size_t i = 0; i < n && o + 4 < cap; i++) {
        o += snprintf(out + o, cap - o, i ? " %02X" : "%02X", b[i]);
    }
}

/* ---- handlers ---- */

static esp_err_t frame_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    cJSON *root = recv_body(req);
    if (!root) return send_error(req, "400 Bad Request", "Body must be JSON (up to 2 KB)");

    cJSON *text = cJSON_GetObjectItem(root, "text");
    cJSON *ent  = cJSON_GetObjectItem(root, "entrance");
    cJSON *aft  = cJSON_GetObjectItem(root, "after");
    cJSON *blk  = cJSON_GetObjectItem(root, "blink");
    cJSON *sh   = cJSON_GetObjectItem(root, "shift");
    cJSON *keep = cJSON_GetObjectItem(root, "keepClock");

    globe_style_t st = { .entrance = -1, .after = GLOBE_AFTER_SPIN, .blink_chars = 0, .shift = -1 };
    const char *err = NULL;

    if (!cJSON_IsString(text)) err = "\"text\" is required";
    if (!err && cJSON_IsNumber(ent)) {
        st.entrance = (int)ent->valuedouble;
        if (st.entrance < 0 || st.entrance >= g_globe_effect_count) err = "\"entrance\" must be 0-37";
    } else if (!err && cJSON_IsString(ent) && ent->valuestring[0]) {
        st.entrance = globe_effect_lookup(ent->valuestring);
        if (st.entrance < 0) err = "Unknown entrance name (see GET /api/v1/effects)";
    }
    if (!err && cJSON_IsString(aft)) {
        const char *a = aft->valuestring;
        if      (!strcmp(a, "spin"))    st.after = GLOBE_AFTER_SPIN;
        else if (!strcmp(a, "hold"))    st.after = GLOBE_AFTER_HOLD;
        else if (!strcmp(a, "flash"))   st.after = GLOBE_AFTER_FLASH;
        else if (!strcmp(a, "instant")) st.after = GLOBE_AFTER_INSTANT;
        else err = "\"after\" must be spin, hold, flash or instant";
    }
    if (cJSON_IsNumber(blk)) st.blink_chars = (int)blk->valuedouble;
    if (cJSON_IsNumber(sh))  st.shift = (int)sh->valuedouble;

    uint8_t enc[GLOBE_TEXT_MAX + 8];
    size_t len = 0;
    int unsupported = 0;
    if (!err) {
        len = globe_text_encode(text->valuestring, enc, sizeof(enc), &unsupported);
        if (len > GLOBE_TEXT_MAX) err = "Text is longer than 35 characters";
    }

    uint8_t f1[GLOBE_FRAME_MAX], f2[GLOBE_FRAME_MAX];
    size_t n1 = 0, n2 = 0;
    if (!err && !globe_frame_compile(enc, len, &st, f1, &n1, f2, &n2)) err = "Could not compile frame";
    bool keep_clock = cJSON_IsTrue(keep);
    char text_copy[96] = "";
    if (cJSON_IsString(text)) snprintf(text_copy, sizeof(text_copy), "%s", text->valuestring);
    cJSON_Delete(root);
    if (err) return send_error(req, "400 Bad Request", err);
    if (g_display_off) return send_error(req, "409 Conflict", DISPLAY_OFF_MSG);

    if (!keep_clock) {
        g_time_on = false;     // otherwise the playlist overwrites the design within a second
        scene_engine_note_manual(text_copy);
    }
    set_pending(NULL, 0, 0);                // a new design cancels any follow-up still waiting
    send_bytes(f1, n1);
    int after_ms = 0;
    if (n2) {
        after_ms = g_globe_effects[st.entrance].duration_ms;
        set_pending(f2, n2, after_ms);
    }

    char hex[GLOBE_FRAME_MAX * 3 + 1];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    hex_of(f1, n1, hex, sizeof(hex));
    cJSON_AddStringToObject(o, "bytes", hex);
    if (n2) {
        hex_of(f2, n2, hex, sizeof(hex));
        cJSON_AddStringToObject(o, "then", hex);
        cJSON_AddNumberToObject(o, "thenAfterMs", after_ms);
    }
    cJSON_AddNumberToObject(o, "unsupported", unsupported);
    ESP_LOGI(TAG, "frame: %s", hex);
    return send_json(req, NULL, o);
}

static esp_err_t clear_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    set_pending(NULL, 0, 0);
    g_time_on = false;
    uint8_t b = GLOBE_CMD_CLEAR;
    send_bytes(&b, 1);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, NULL, o);
}

static esp_err_t effects_get(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_globe_effect_count; i++) {
        const globe_effect_info_t *e = &g_globe_effects[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", e->id);
        cJSON_AddStringToObject(o, "name", e->name);
        cJSON_AddStringToObject(o, "group", e->group);
        cJSON_AddNumberToObject(o, "durationMs", e->duration_ms);
        if (e->same_as >= 0) cJSON_AddNumberToObject(o, "sameAs", e->same_as);
        cJSON_AddItemToArray(arr, o);
    }
    return send_json(req, NULL, arr);
}

static esp_err_t options_any(httpd_req_t *req)
{
    cors(req);
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");    // fewer preflights from a dev server
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t designer_get(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/designer.html.gz", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "designer.html.gz not on SPIFFS (run npm run build in web/)");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    char *buf = malloc(2048);
    if (!buf) { fclose(f); return ESP_FAIL; }
    size_t n;
    esp_err_t r = ESP_OK;
    while ((n = fread(buf, 1, 2048, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) { r = ESP_FAIL; break; }
    }
    free(buf);
    fclose(f);
    if (r == ESP_OK) httpd_resp_send_chunk(req, NULL, 0);
    return r;
}


/* ---- scenes, playlist, now showing (Phase 2) ---- */

static bool uri_id(httpd_req_t *req, const char *prefix, char *id, size_t cap)
{
    const char *p = req->uri + strlen(prefix);
    size_t n = strcspn(p, "?#");
    if (n == 0 || n >= cap) return false;
    memcpy(id, p, n);
    id[n] = '\0';
    return scene_id_valid(id);
}

static esp_err_t ok_json(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, NULL, o);
}

/* Check a scene body: {"name":..,"frames":[{text, entrance, after, blink, shift, seconds}]} */
static const char *scene_check(cJSON *sc)
{
    cJSON *frames = cJSON_GetObjectItem(sc, "frames");
    int n = cJSON_GetArraySize(frames);
    if (!cJSON_IsArray(frames) || n < 1) return "A scene needs at least one frame";
    if (n > SCENE_MAX_FRAMES) return "Too many frames (max 24)";
    cJSON *f;
    cJSON_ArrayForEach(f, frames) {
        cJSON *t = cJSON_GetObjectItem(f, "text");
        if (!cJSON_IsString(t) || strlen(t->valuestring) > 95) return "Every frame needs text (up to 95 characters before values are filled in)";
        cJSON *e = cJSON_GetObjectItem(f, "entrance");
        if (cJSON_IsNumber(e) && (e->valuedouble < 0 || e->valuedouble >= g_globe_effect_count)) return "entrance must be 0-37";
        if (cJSON_IsString(e) && globe_effect_lookup(e->valuestring) < 0) return "Unknown entrance name";
    }
    return NULL;
}

static esp_err_t scenes_get(httpd_req_t *req)
{
    return send_json(req, NULL, scene_list());
}

static esp_err_t scene_get(httpd_req_t *req)
{
    char id[SCENE_ID_MAX];
    if (!uri_id(req, "/api/v1/scenes/", id, sizeof(id))) return send_error(req, "400 Bad Request", "Scene ids use a-z, 0-9 and -");
    cJSON *s = scene_load(id);
    if (!s) return send_error(req, "404 Not Found", "No such scene");
    return send_json(req, NULL, s);
}

static esp_err_t scene_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    char id[SCENE_ID_MAX];
    if (!uri_id(req, "/api/v1/scenes/", id, sizeof(id))) return send_error(req, "400 Bad Request", "Scene ids use a-z, 0-9 and - (up to 23)");
    cJSON *sc = recv_body_max(req, 16 * 1024);
    if (!sc) return send_error(req, "400 Bad Request", "Body must be a JSON scene (up to 16 KB)");
    const char *err = scene_check(sc);
    if (err) { cJSON_Delete(sc); return send_error(req, "400 Bad Request", err); }
    cJSON_DeleteItemFromObject(sc, "id");
    cJSON_AddStringToObject(sc, "id", id);
    cJSON_DeleteItemFromObject(sc, "builtin");
    bool ok = scene_save(id, sc);
    cJSON_Delete(sc);
    return ok ? ok_json(req) : send_error(req, "500 Internal Server Error", "Could not save the scene");
}

static esp_err_t scene_del(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    char id[SCENE_ID_MAX];
    if (!uri_id(req, "/api/v1/scenes/", id, sizeof(id))) return send_error(req, "400 Bad Request", "Bad scene id");
    scene_delete(id);
    return ok_json(req);
}

static esp_err_t playlist_get(httpd_req_t *req)
{
    return send_json(req, NULL, playlist_load());
}

static esp_err_t playlist_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    cJSON *p = recv_body_max(req, 4096);
    cJSON *items = p ? cJSON_GetObjectItem(p, "items") : NULL;
    if (!cJSON_IsArray(items)) { cJSON_Delete(p); return send_error(req, "400 Bad Request", "Need {items:[{scene,seconds}]}"); }
    if (cJSON_GetArraySize(items) > PLAYLIST_MAX) { cJSON_Delete(p); return send_error(req, "400 Bad Request", "Too many playlist items (max 24)"); }
    cJSON *it;
    cJSON_ArrayForEach(it, items) {
        cJSON *sc = cJSON_GetObjectItem(it, "scene");
        if (!cJSON_IsString(sc) || !scene_id_valid(sc->valuestring)) { cJSON_Delete(p); return send_error(req, "400 Bad Request", "Every item needs a valid scene id"); }
    }
    bool ok = playlist_save(p);
    cJSON_Delete(p);
    if (ok) g_time_on = true;                 // show the new rotation
    return ok ? ok_json(req) : send_error(req, "500 Internal Server Error", "Could not save the playlist");
}

/* {"scene":"id","seconds":30}  or  {"frames":[...],"seconds":30} (a quick message, saved as scene "quick") */
static esp_err_t play_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    cJSON *b = recv_body_max(req, 16 * 1024);
    if (!b) return send_error(req, "400 Bad Request", "Body must be JSON");
    if (g_display_off) { cJSON_Delete(b); return send_error(req, "409 Conflict", DISPLAY_OFF_MSG); }
    cJSON *sc = cJSON_GetObjectItem(b, "scene");
    cJSON *se = cJSON_GetObjectItem(b, "seconds");
    unsigned seconds = cJSON_IsNumber(se) && se->valuedouble > 0 ? (unsigned)se->valuedouble : 0;
    char id[SCENE_ID_MAX] = "quick";
    const char *err = NULL;
    if (cJSON_IsArray(cJSON_GetObjectItem(b, "frames"))) {
        err = scene_check(b);
        if (!err) {
            cJSON *copy = cJSON_CreateObject();
            cJSON_AddStringToObject(copy, "name", "Quick message");
            cJSON_AddItemToObject(copy, "frames", cJSON_Duplicate(cJSON_GetObjectItem(b, "frames"), true));
            if (!scene_save(id, copy)) err = "Could not save the message";
            cJSON_Delete(copy);
        }
    } else if (cJSON_IsString(sc) && scene_id_valid(sc->valuestring)) {
        snprintf(id, sizeof(id), "%s", sc->valuestring);
    } else {
        err = "Need scene or frames";
    }
    cJSON_Delete(b);
    if (err) return send_error(req, "400 Bad Request", err);
    if (!scene_engine_play(id, seconds)) return send_error(req, "404 Not Found", "No such scene");
    g_time_on = true;
    return ok_json(req);
}

/* The kitchen timer.
 *   POST /api/v1/timer  {"seconds":300,"label":"TEA"}   start (replaces a running one); {"seconds":0} cancels or dismisses
 *   GET  /api/v1/timer  {"remaining":287} */
static esp_err_t timer_get(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "remaining", scene_engine_timer_left());
    return send_json(req, NULL, o);
}

static esp_err_t timer_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    cJSON *b = recv_body(req);
    cJSON *se = b ? cJSON_GetObjectItem(b, "seconds") : NULL, *lb = b ? cJSON_GetObjectItem(b, "label") : NULL;
    if (!cJSON_IsNumber(se) || se->valuedouble < 0) { cJSON_Delete(b); return send_error(req, "400 Bad Request", "Need {\"seconds\":300} (0 cancels), optionally \"label\""); }
    unsigned seconds = (unsigned)(se->valuedouble > 86399 ? 86399 : se->valuedouble);
    if (seconds && g_display_off) { cJSON_Delete(b); return send_error(req, "409 Conflict", DISPLAY_OFF_MSG); }
    bool ok = scene_engine_timer(seconds, cJSON_IsString(lb) ? lb->valuestring : "");
    cJSON_Delete(b);
    if (ok && seconds) g_time_on = true;
    return ok ? ok_json(req) : send_error(req, "500 Internal Server Error", "Could not start the timer");
}

/* POST /api/v1/webapp  body: the built web app (hello_world/main/data/designer.html.gz).
 * Replaces the page at /designer without USB: it lives on the "spiffs" partition,
 * which a firmware update (POST /ota) doesn't touch. Written to a new file
 * first, so a cut-off upload leaves the old page in place. */
#define WEBAPP_PATH "/spiffs/designer.html.gz"
#define WEBAPP_NEW  "/spiffs/designer.new"
#define WEBAPP_MAX  (400 * 1024)

static void drain(httpd_req_t *req, int left)
{
    char junk[256];
    while (left > 0) {
        int r = httpd_req_recv(req, junk, left < (int)sizeof(junk) ? left : (int)sizeof(junk));
        if (r <= 0) break;
        left -= r;
    }
}

static esp_err_t webapp_post(httpd_req_t *req)
{
    if (!auth_check(req)) { drain(req, req->content_len); return ESP_OK; }
    if (req->content_len < 1024 || req->content_len > WEBAPP_MAX) {
        drain(req, req->content_len);
        return send_error(req, "400 Bad Request", "Send the built designer.html.gz (1 KB to 400 KB)");
    }
    char *buf = malloc(4096);
    FILE *f = buf ? fopen(WEBAPP_NEW, "w") : NULL;
    if (!f) { free(buf); drain(req, req->content_len); return send_error(req, "500 Internal Server Error", "Could not open the web app file"); }
    const char *err = NULL;
    size_t got = 0;
    while (got < req->content_len && !err) {
        int r = httpd_req_recv(req, buf, req->content_len - got < 4096 ? req->content_len - got : 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { err = "The upload was cut off"; break; }
        if (got == 0 && (r < 2 || (uint8_t)buf[0] != 0x1f || (uint8_t)buf[1] != 0x8b)) err = "That isn't a gzip file (send designer.html.gz)";
        else if (fwrite(buf, 1, r, f) != (size_t)r) err = "The globe ran out of room for the web app";
        got += r;
    }
    bool closed = fclose(f) == 0;
    free(buf);
    if (!err && !closed) err = "Could not write the web app file";
    if (err) {
        remove(WEBAPP_NEW);
        drain(req, req->content_len - got);
        return send_error(req, "400 Bad Request", err);
    }
    remove(WEBAPP_PATH);
    if (rename(WEBAPP_NEW, WEBAPP_PATH) != 0) return send_error(req, "500 Internal Server Error", "Could not put the new web app in place");
    ESP_LOGI(TAG, "Web app replaced over the network (%u bytes)", (unsigned)got);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddNumberToObject(o, "bytes", (double)got);
    return send_json(req, NULL, o);
}

/* GET /api/v1/version  {"version":"0.9.0-beta","built":"Nov 15 2026 10:40:00","idf":"v6.1","slot":"ota_1","state":"kept"}
 * state: "probation" (arrived over the network, not yet kept), "kept", or "usb" (flashed over USB). */
static esp_err_t version_get(httpd_req_t *req)
{
    const esp_app_desc_t *d = esp_app_get_description();
    char built[40];
    snprintf(built, sizeof(built), "%s %s", d->date, d->time);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "version", d->version);
    cJSON_AddStringToObject(o, "built", built);
    cJSON_AddStringToObject(o, "idf", d->idf_ver);
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    bool known = run && esp_ota_get_state_partition(run, &st) == ESP_OK;
    if (run) cJSON_AddStringToObject(o, "slot", run->label);
    cJSON_AddStringToObject(o, "state", !known || st == ESP_OTA_IMG_UNDEFINED ? "usb" : st == ESP_OTA_IMG_PENDING_VERIFY ? "probation" : "kept");
    return send_json(req, NULL, o);
}

static esp_err_t resume_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    g_time_on = true;
    scene_engine_resume();
    return ok_json(req);
}

static esp_err_t next_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    g_time_on = true;
    scene_engine_next();
    return ok_json(req);
}

static esp_err_t settings_get(httpd_req_t *req)
{
    return send_json(req, NULL, settings_load());
}

static esp_err_t settings_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    cJSON *b = recv_body(req);
    if (!cJSON_IsObject(b)) { cJSON_Delete(b); return send_error(req, "400 Bad Request", "Body must be a JSON object"); }
    bool ok = settings_save(b);
    cJSON_Delete(b);
    scene_engine_refresh();                 // settings only: no need to restart what is playing
    feeds_settings_changed();
    return ok ? ok_json(req) : send_error(req, "500 Internal Server Error", "Could not save settings");
}

/* ---- pushed values ----
 *   GET  /api/v1/values            {"cpu":"42","build":"PASS"}
 *   POST /api/v1/values[?ttl=600]  {"cpu":42,"build":"PASS","old":null}
 * Strings and numbers set a value, null removes it. With ?ttl=N the values
 * expire N seconds after the last update. Show them with {var:cpu} or {cpu}. */

static esp_err_t values_get(httpd_req_t *req)
{
    return send_json(req, NULL, value_store_json());
}

static esp_err_t values_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    cJSON *b = recv_body_max(req, 4096);
    if (!cJSON_IsObject(b)) { cJSON_Delete(b); return send_error(req, "400 Bad Request", "Body must be a JSON object such as {\"cpu\":42}"); }

    uint32_t ttl = 0;
    size_t qlen = httpd_req_get_url_query_len(req);    // sized to fit: ?token=... can be long
    char *query = qlen ? malloc(qlen + 1) : NULL;
    char num[12];
    if (query && httpd_req_get_url_query_str(req, query, qlen + 1) == ESP_OK &&
        httpd_query_key_value(query, "ttl", num, sizeof(num)) == ESP_OK) {
        long t = atol(num);
        if (t > 0) ttl = (uint32_t)t;
    }
    free(query);

    const char *err = NULL;
    const cJSON *it;
    cJSON_ArrayForEach(it, b) {                         // check everything before changing anything
        if (!value_key_valid(it->string)) err = "Value names use a-z, 0-9 and _ (up to 15 characters)";
        else if (!cJSON_IsString(it) && !cJSON_IsNumber(it) && !cJSON_IsNull(it) && !cJSON_IsBool(it)) err = "Values must be text, numbers, true/false or null";
        else if (cJSON_IsString(it) && strlen(it->valuestring) >= VALUE_TEXT_MAX) err = "Values can be up to 47 characters";
    }
    if (!err) {
        cJSON_ArrayForEach(it, b) {
            bool ok = true;
            char text[32];
            if (cJSON_IsNull(it)) {
                value_store_delete(it->string);
            } else if (cJSON_IsString(it)) {
                ok = value_store_set(it->string, it->valuestring, ttl);
            } else if (cJSON_IsBool(it)) {
                ok = value_store_set(it->string, cJSON_IsTrue(it) ? "YES" : "NO", ttl);
            } else {
                double v = it->valuedouble;
                if (v == (double)(long long)v && v > -1e15 && v < 1e15) snprintf(text, sizeof(text), "%lld", (long long)v);
                else snprintf(text, sizeof(text), "%.6g", v);
                ok = value_store_set(it->string, text, ttl);
            }
            if (!ok) err = "The globe is holding too many values (max 64)";
        }
    }
    cJSON_Delete(b);
    return err ? send_error(req, "400 Bad Request", err) : ok_json(req);
}

static esp_err_t now_get(httpd_req_t *req)
{
    return send_json(req, NULL, scene_engine_status());
}

/* ---- access token (auth.c) ----
 *   GET  /api/v1/auth   {"required":true,"valid":false,"minLength":8,"maxLength":64}
 *                       valid: this request carries the right token (or none is needed)
 *   POST /api/v1/auth   {"token":"new-token"}; "" removes it. Needs the current token when one is set.
 * The token itself is never returned. */

static esp_err_t auth_get(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "required", auth_required());
    cJSON_AddBoolToObject(o, "valid", auth_request_ok(req));
    cJSON_AddNumberToObject(o, "minLength", AUTH_TOKEN_MIN);
    cJSON_AddNumberToObject(o, "maxLength", AUTH_TOKEN_MAX);
    return send_json(req, NULL, o);
}

static esp_err_t auth_post(httpd_req_t *req)
{
    if (!auth_check(req)) return ESP_OK;
    cJSON *b = recv_body(req);
    cJSON *t = b ? cJSON_GetObjectItem(b, "token") : NULL;
    const char *err = NULL, *status = "400 Bad Request";
    if (!cJSON_IsString(t)) err = "Need {\"token\":\"...\"} (an empty string removes the token)";
    else if (!auth_token_valid(t->valuestring)) err = "Tokens are 8-64 characters: A-Z, a-z, 0-9 and . _ ~ -";
    else if (!auth_set(t->valuestring)) { err = "Could not save the token"; status = "500 Internal Server Error"; }
    cJSON_Delete(b);
    if (err) return send_error(req, status, err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddBoolToObject(o, "required", auth_required());
    return send_json(req, NULL, o);
}

void api_v1_register(httpd_handle_t server, QueueHandle_t display_queue)
{
    s_queue = display_queue;
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(pending_task, "frame_then", 3072, NULL, 4, &s_pending_task);

    static const httpd_uri_t routes[] = {
        { .uri = "/api/v1/frame",   .method = HTTP_POST,    .handler = frame_post   },
        { .uri = "/api/v1/clear",   .method = HTTP_POST,    .handler = clear_post   },
        { .uri = "/api/v1/effects", .method = HTTP_GET,     .handler = effects_get  },
        { .uri = "/api/v1/*",       .method = HTTP_OPTIONS, .handler = options_any  },
        { .uri = "/designer",       .method = HTTP_GET,     .handler = designer_get },
        { .uri = "/api/v1/scenes",   .method = HTTP_GET,    .handler = scenes_get    },
        { .uri = "/api/v1/scenes/*", .method = HTTP_GET,    .handler = scene_get     },
        { .uri = "/api/v1/scenes/*", .method = HTTP_POST,   .handler = scene_post    },
        { .uri = "/api/v1/scenes/*", .method = HTTP_DELETE, .handler = scene_del     },
        { .uri = "/api/v1/playlist", .method = HTTP_GET,    .handler = playlist_get  },
        { .uri = "/api/v1/playlist", .method = HTTP_POST,   .handler = playlist_post },
        { .uri = "/api/v1/play",     .method = HTTP_POST,   .handler = play_post     },
        { .uri = "/api/v1/resume",   .method = HTTP_POST,   .handler = resume_post   },
        { .uri = "/api/v1/now",      .method = HTTP_GET,    .handler = now_get       },
        { .uri = "/api/v1/next",     .method = HTTP_POST,   .handler = next_post     },
        { .uri = "/api/v1/timer",    .method = HTTP_GET,    .handler = timer_get     },
        { .uri = "/api/v1/version",  .method = HTTP_GET,    .handler = version_get   },
        { .uri = "/api/v1/webapp",   .method = HTTP_POST,   .handler = webapp_post   },
        { .uri = "/api/v1/timer",    .method = HTTP_POST,   .handler = timer_post    },
        { .uri = "/api/v1/settings", .method = HTTP_GET,    .handler = settings_get  },
        { .uri = "/api/v1/settings", .method = HTTP_POST,   .handler = settings_post },
        { .uri = "/api/v1/values",   .method = HTTP_GET,    .handler = values_get    },
        { .uri = "/api/v1/values",   .method = HTTP_POST,   .handler = values_post   },
        { .uri = "/api/v1/auth",     .method = HTTP_GET,    .handler = auth_get      },
        { .uri = "/api/v1/auth",     .method = HTTP_POST,   .handler = auth_post     },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        if (httpd_register_uri_handler(server, &routes[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Could not register %s", routes[i].uri);
        }
    }
    live_register(server);                    // GET /api/v1/ws
}
