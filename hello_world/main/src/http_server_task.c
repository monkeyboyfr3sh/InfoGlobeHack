#include <errno.h>
#include "http_server_task.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "nvs.h"

#include "message_type.h"
#include "infoglobe_animations.h"
#include "frame_maker_task.h"
#include "ir_led_task.h"
#include "api_v1.h"

static const char *TAG = "HTTP_SERVER";

#define NUM_BUTTONS     6
#define CMD_MAX_LEN     64

static QueueHandle_t s_display_queue = NULL;
static char s_button_cmds[NUM_BUTTONS][CMD_MAX_LEN];

/* ------------------------------------------------------------------ */
/*  SPIFFS                                                              */
/* ------------------------------------------------------------------ */

static void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info("spiffs", &total, &used);
        ESP_LOGI(TAG, "SPIFFS: %d/%d bytes used", used, total);
    }
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static cJSON *recv_json(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 512) return NULL;
    char *buf = malloc(req->content_len + 1);
    if (!buf) return NULL;
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) { free(buf); return NULL; }
    buf[received] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    return root;
}

/* ------------------------------------------------------------------ */
/*  Handlers                                                            */
/* ------------------------------------------------------------------ */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/index.html", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "index.html not found on SPIFFS");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t text_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }

    cJSON *text  = cJSON_GetObjectItem(root, "text");
    cJSON *blink = cJSON_GetObjectItem(root, "blink");

    if (cJSON_IsString(text) && text->valuestring[0] != '\0') {
        const char *msg = text->valuestring;
        int len = strlen(msg);
        uint8_t blink_enable = cJSON_IsTrue(blink) ? 1 : 0;
        string_with_blink_shift(s_display_queue, msg, len,
                                blink_enable ? (len + 1) : 0,
                                len);
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t raw_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }

    cJSON *hex = cJSON_GetObjectItem(root, "hex");
    if (cJSON_IsString(hex)) {
        const char *hex_str = hex->valuestring;
        int hex_len = strlen(hex_str);
        int byte_len = hex_len / 2;
        if (byte_len > 0) {
            uint8_t *bytes = malloc(byte_len);
            if (bytes) {
                for (int i = 0; i < byte_len; i++) {
                    char b[3] = { hex_str[i * 2], hex_str[i * 2 + 1], '\0' };
                    bytes[i] = (uint8_t)strtol(b, NULL, 16);
                }
                push_buffer_to_queue(s_display_queue, bytes, byte_len);
            }
        }
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t button_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }

    cJSON *button  = cJSON_GetObjectItem(root, "button");
    cJSON *config  = cJSON_GetObjectItem(root, "config");
    cJSON *command = cJSON_GetObjectItem(root, "command");

    if (cJSON_IsNumber(button)) {
        int idx = (int)button->valuedouble - 1;
        if (idx >= 0 && idx < NUM_BUTTONS) {
            if (cJSON_IsTrue(config) && cJSON_IsString(command)) {
                strncpy(s_button_cmds[idx], command->valuestring, CMD_MAX_LEN - 1);
                s_button_cmds[idx][CMD_MAX_LEN - 1] = '\0';
                ESP_LOGI(TAG, "Button %d set to: '%s'", idx + 1, s_button_cmds[idx]);
            } else {
                const char *cmd = s_button_cmds[idx];
                if (strlen(cmd) > 0) {
                    string_with_blink_shift(s_display_queue, cmd, strlen(cmd), 0, strlen(cmd));
                } else {
                    ESP_LOGW(TAG, "Button %d has no command configured", idx + 1);
                }
            }
        }
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
            "No OTA partition found. Add ota_0/ota_1 partitions to your partition table.");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char *buf = malloc(1024);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    while (remaining > 0) {
        int to_recv = remaining < 1024 ? remaining : 1024;
        int recv_len = httpd_req_recv(req, buf, to_recv);
        if (recv_len <= 0) {
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }
        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }
        remaining -= recv_len;
        ESP_LOGD(TAG, "OTA progress: %d bytes remaining", remaining);
    }
    free(buf);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Image validation failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete, rebooting...");
    httpd_resp_sendstr(req, "OK");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Server start                                                        */
static esp_err_t mode_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        char resp[32];
        snprintf(resp, sizeof(resp), "{\"time_mode\":%s}", g_time_on ? "true" : "false");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, resp);
        return ESP_OK;
    }

    cJSON *root = recv_json(req);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }

    cJSON *time_mode = cJSON_GetObjectItem(root, "time_mode");
    if (cJSON_IsBool(time_mode)) {
        g_time_on = cJSON_IsTrue(time_mode);
        ESP_LOGI(TAG, "Time mode set to: %s", g_time_on ? "on" : "off");
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  IR link: gap tuning, stats and a max-rate test pattern              */
/* ------------------------------------------------------------------ */

static volatile bool s_ir_test_on = false;
static TaskHandle_t s_ir_test_task = NULL;

/* Sends "TEST nnn" frames as fast as the link accepts them (the display
 * queue blocks), pausing the clock while it runs. Skipped or garbled
 * numbers on the globe mean the gap is too short. */
static void ir_test_task(void *arg)
{
    bool prev_time_on = g_time_on;
    g_time_on = false;
    unsigned n = 0;
    char txt[9];
    while (s_ir_test_on) {
        snprintf(txt, sizeof(txt), "TEST %03u", n);
        n = (n + 1) % 1000;
        string_with_blink_shift(s_display_queue, txt, 8, 0, 8);
    }
    g_time_on = prev_time_on;
    s_ir_test_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t ir_handler(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        cJSON *root = recv_json(req);
        if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }
        cJSON *gap = cJSON_GetObjectItem(root, "gap_ms");
        if (cJSON_IsNumber(gap) && gap->valuedouble >= 0) {
            ir_set_frame_gap_ms((uint32_t)gap->valuedouble);
        }
        cJSON *test = cJSON_GetObjectItem(root, "test");
        if (cJSON_IsBool(test)) {
            bool on = cJSON_IsTrue(test);
            if (on && s_ir_test_task == NULL) {
                s_ir_test_on = true;
                xTaskCreate(ir_test_task, "ir_test", 3072, NULL, 4, &s_ir_test_task);
                ESP_LOGI(TAG, "IR test pattern started");
            } else if (!on && s_ir_test_on) {
                s_ir_test_on = false;
                // Wait for it to finish its last frame and restore the clock
                // setting, so a following /mode request isn't overridden.
                for (int i = 0; i < 50 && s_ir_test_task != NULL; i++) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                ESP_LOGI(TAG, "IR test pattern stopped");
            }
        }
        cJSON_Delete(root);
    }

    ir_stats_t st;
    ir_get_stats(&st);
    char resp[160];
    snprintf(resp, sizeof(resp),
             "{\"gap_ms\":%u,\"fps\":%.1f,\"busy_pct\":%.0f,\"frames\":%u,\"test\":%s}",
             (unsigned)st.gap_ms, st.fps, st.busy_pct, (unsigned)st.frames,
             s_ir_test_on ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Protocol explorer notes: {"key": "note (markdown)", ...}            */
/*  Stored in /notes/notes.json on the dedicated "notes" SPIFFS         */
/*  partition, which is not part of the flash image, so normal flashing */
/*  never touches it. Older notes in NVS are migrated on first load.    */
/* ------------------------------------------------------------------ */

#define NOTES_BASE        "/notes"
#define NOTES_FILE        NOTES_BASE "/notes.json"
#define NOTES_TMP         NOTES_BASE "/notes.tmp"
#define NOTES_MAX_BYTES   (96 * 1024)      // whole JSON file
#define NOTE_POST_MAX     (16 * 1024)      // one POST body
#define NOTES_LEGACY_NS   "explorer"       // pre-SPIFFS storage (NVS blob)
#define NOTES_LEGACY_KEY  "notes"

static bool s_notes_fs_ok = false;

static void init_notes_fs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = NOTES_BASE,
        .partition_label = "notes",
        .max_files = 3,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    s_notes_fs_ok = (err == ESP_OK);
    if (!s_notes_fs_ok) {
        ESP_LOGE(TAG, "Notes partition mount failed: %s", esp_err_to_name(err));
    }
}

static char *read_file(const char *path, size_t max)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = NULL;
    if (n >= 0 && (size_t)n <= max) {
        buf = malloc(n + 1);
        if (buf && fread(buf, 1, n, f) == (size_t)n) {
            buf[n] = '\0';
        } else {
            free(buf);
            buf = NULL;
        }
    }
    fclose(f);
    return buf;
}

static cJSON *notes_load_legacy_nvs(void)
{
    cJSON *obj = NULL;
    nvs_handle_t h;
    if (nvs_open(NOTES_LEGACY_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = 0;
        if (nvs_get_blob(h, NOTES_LEGACY_KEY, NULL, &n) == ESP_OK && n > 0 && n <= 8192) {
            char *buf = malloc(n);
            if (buf && nvs_get_blob(h, NOTES_LEGACY_KEY, buf, &n) == ESP_OK) {
                buf[n - 1] = '\0';
                obj = cJSON_Parse(buf);
            }
            free(buf);
        }
        nvs_close(h);
    }
    return obj;
}

static bool notes_save(const cJSON *notes)
{
    if (!s_notes_fs_ok) {
        return false;
    }
    char *out = cJSON_PrintUnformatted(notes);
    bool ok = false;
    if (out && strlen(out) <= NOTES_MAX_BYTES) {
        // Write a temp file, then swap it in, so a reset mid-write can't
        // leave a half-written notes file behind.
        // SPIFFS only reclaims pages from deleted/overwritten files during
        // garbage collection; rewriting a ~50 KB file over and over runs it
        // out of free blocks (ENOSPC) long before the partition is full.
        // Drop any half-written temp file and make room up front.
        remove(NOTES_TMP);
        esp_err_t gc = esp_spiffs_gc("notes", strlen(out) + 4096);
        if (gc != ESP_OK) {
            ESP_LOGW(TAG, "notes: gc returned %s", esp_err_to_name(gc));
        }
        FILE *f = fopen(NOTES_TMP, "w");
        if (!f) {
            ESP_LOGE(TAG, "notes: fopen tmp failed, errno %d", errno);
        } else {
            ok = fputs(out, f) >= 0;
            if (!ok) ESP_LOGE(TAG, "notes: write failed, errno %d", errno);
            if (fclose(f) != 0) { ESP_LOGE(TAG, "notes: close failed, errno %d", errno); ok = false; }
        }
        if (ok) {
            if (remove(NOTES_FILE) != 0) ESP_LOGW(TAG, "notes: remove old failed, errno %d", errno);
            ok = rename(NOTES_TMP, NOTES_FILE) == 0;
            if (!ok) ESP_LOGE(TAG, "notes: rename failed, errno %d", errno);
        }
    }
    if (!ok) {
        size_t total = 0, used = 0;
        esp_spiffs_info("notes", &total, &used);
        ESP_LOGE(TAG, "Notes save failed: %u bytes, fs %u/%u used, largest free block %u",
                 (unsigned)(out ? strlen(out) : 0), (unsigned)used, (unsigned)total,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
    free(out);
    return ok;
}

static cJSON *notes_load(void)
{
    cJSON *obj = NULL;
    char *buf = s_notes_fs_ok ? read_file(NOTES_FILE, NOTES_MAX_BYTES) : NULL;
    if (buf) {
        obj = cJSON_Parse(buf);
        free(buf);
    } else if (s_notes_fs_ok) {
        // First run on the new storage: bring over notes saved in NVS.
        obj = notes_load_legacy_nvs();
        if (cJSON_IsObject(obj) && notes_save(obj)) {
            nvs_handle_t h;
            if (nvs_open(NOTES_LEGACY_NS, NVS_READWRITE, &h) == ESP_OK) {
                nvs_erase_key(h, NOTES_LEGACY_KEY);
                nvs_commit(h);
                nvs_close(h);
            }
            ESP_LOGI(TAG, "Migrated %d explorer notes from NVS", cJSON_GetArraySize(obj));
        }
    }
    if (!cJSON_IsObject(obj)) {
        cJSON_Delete(obj);
        obj = cJSON_CreateObject();
    }
    return obj;
}

/* Like recv_json() but with room for long, multi-line notes. */
static cJSON *recv_json_large(httpd_req_t *req, size_t max)
{
    if (req->content_len == 0 || req->content_len > max) {
        return NULL;
    }
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        return NULL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        got += r;
    }
    buf[got] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    return root;
}

static esp_err_t notes_handler(httpd_req_t *req)
{
    cJSON *notes = notes_load();

    if (req->method == HTTP_POST) {
        cJSON *root = recv_json_large(req, NOTE_POST_MAX);
        cJSON *key  = root ? cJSON_GetObjectItem(root, "key")  : NULL;
        cJSON *note = root ? cJSON_GetObjectItem(root, "note") : NULL;
        if (!cJSON_IsString(key) || !key->valuestring[0] || !cJSON_IsString(note)) {
            cJSON_Delete(root);
            cJSON_Delete(notes);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Need {key, note} (note up to 16 KB)");
            return ESP_FAIL;
        }
        cJSON_DeleteItemFromObject(notes, key->valuestring);
        if (note->valuestring[0]) {               // empty note deletes the entry
            cJSON_AddStringToObject(notes, key->valuestring, note->valuestring);
        }
        cJSON_Delete(root);

        if (!notes_save(notes)) {
            cJSON_Delete(notes);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save notes");
            return ESP_FAIL;
        }
    }

    char *out = cJSON_PrintUnformatted(notes);
    cJSON_Delete(notes);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out ? out : "{}");
    free(out);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

httpd_handle_t start_http_server(QueueHandle_t display_queue)
{
    s_display_queue = display_queue;
    memset(s_button_cmds, 0, sizeof(s_button_cmds));
    init_spiffs();
    init_notes_fs();

    httpd_config_t config      = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers    = 48;
    config.uri_match_fn        = httpd_uri_match_wildcard;   // for /api/v1/* (OPTIONS)
    config.stack_size          = 8192;
    config.recv_wait_timeout   = 60; // seconds — needed for OTA uploads
    config.send_wait_timeout   = 60;
    config.lru_purge_enable    = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = root_get_handler   },
        { .uri = "/text",   .method = HTTP_POST, .handler = text_post_handler  },
        { .uri = "/raw",    .method = HTTP_POST, .handler = raw_post_handler   },
        { .uri = "/button", .method = HTTP_POST, .handler = button_post_handler},
        { .uri = "/ota",    .method = HTTP_POST, .handler = ota_post_handler   },
        { .uri = "/mode",   .method = HTTP_GET,  .handler = mode_handler       },
        { .uri = "/mode",   .method = HTTP_POST, .handler = mode_handler       },
        { .uri = "/ir",     .method = HTTP_GET,  .handler = ir_handler         },
        { .uri = "/ir",     .method = HTTP_POST, .handler = ir_handler         },
        { .uri = "/notes",  .method = HTTP_GET,  .handler = notes_handler      },
        { .uri = "/notes",  .method = HTTP_POST, .handler = notes_handler      },
    };

    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    api_v1_register(server, display_queue);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return server;
}
