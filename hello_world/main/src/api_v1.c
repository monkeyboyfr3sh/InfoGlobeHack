/* REST API v1: designed frames in, protocol bytes out (via globe_frame).
 *
 *   POST /api/v1/frame   {"text":"SERVER OK","entrance":"Pincer"|2|null,
 *                         "after":"spin|hold|flash|instant","blink":2,"shift":-1,
 *                         "keepClock":false}
 *        -> {"ok":true,"bytes":"00 00 ...","then":"05 ...","thenAfterMs":1500,"unsupported":0}
 *   POST /api/v1/clear   blank the display
 *   GET  /api/v1/effects the effect table (id, name, group, durationMs, sameAs)
 *   GET  /designer       the Vite-built designer (designer.html.gz on SPIFFS)
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

static const char *TAG = "API_V1";

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
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
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

static cJSON *recv_body(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 2048) return NULL;
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
    cJSON_Delete(root);
    if (err) return send_error(req, "400 Bad Request", err);

    if (!keep_clock) g_time_on = false;     // otherwise the clock overwrites the design within a second
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
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        if (httpd_register_uri_handler(server, &routes[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Could not register %s", routes[i].uri);
        }
    }
}
