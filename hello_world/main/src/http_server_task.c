#include "http_server_task.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_spiffs.h"
#include "cJSON.h"

#include "message_type.h"
#include "infoglobe_animations.h"
#include "frame_maker_task.h"
#include "ir_led_task.h"

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

httpd_handle_t start_http_server(QueueHandle_t display_queue)
{
    s_display_queue = display_queue;
    memset(s_button_cmds, 0, sizeof(s_button_cmds));
    init_spiffs();

    httpd_config_t config      = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers    = 12;
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
    };

    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return server;
}
