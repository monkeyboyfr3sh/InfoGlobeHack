#include "http_server_task.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "cJSON.h"

#include "message_type.h"
#include "infoglobe_animations.h"

static const char *TAG = "HTTP_SERVER";

#define NUM_BUTTONS     6
#define CMD_MAX_LEN     64

static QueueHandle_t s_display_queue = NULL;
static char s_button_cmds[NUM_BUTTONS][CMD_MAX_LEN];

/* ------------------------------------------------------------------ */
/*  Embedded HTML                                                       */
/* ------------------------------------------------------------------ */

static const char INDEX_HTML[] =
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>InfoGlobe</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:16px;max-width:520px;margin:0 auto}"
"h1{text-align:center;color:#e94560;margin:16px 0}"
".card{background:#16213e;border-radius:8px;padding:16px;margin-bottom:12px}"
"h2{color:#e94560;font-size:1rem;margin-bottom:10px}"
"input[type=text],input[type=file]{width:100%;padding:8px;background:#0f3460;color:#eee;border:1px solid #e94560;border-radius:4px;margin-bottom:8px}"
"button{background:#e94560;color:#fff;border:none;padding:8px 14px;border-radius:4px;cursor:pointer;margin:3px}"
"button:hover{background:#c73652}"
"label{font-size:.9rem;display:flex;align-items:center;gap:4px}"
".row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}"
".grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}"
"#toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);padding:10px 20px;border-radius:6px;display:none;font-weight:bold}"
".ok{background:#155724;color:#d4edda}"
".err{background:#721c24;color:#f8d7da}"
"progress{width:100%;margin-top:8px;accent-color:#e94560}"
"</style>"
"</head>"
"<body>"
"<h1>InfoGlobe</h1>"
"<div id='toast'></div>"

"<div class='card'><h2>Send Text</h2>"
"<input type='text' id='txt' placeholder='Message...'>"
"<div class='row'>"
"<label><input type='checkbox' id='blink'> Blink</label>"
"<button onclick='sendText()'>Send Text</button>"
"<button onclick='sendRaw()'>Send Raw Hex</button>"
"</div></div>"

"<div class='card'><h2>Buttons</h2>"
"<div class='row' style='margin-bottom:8px'>"
"<input type='text' id='cmd' placeholder='Configure button command...' style='flex:1;margin:0'>"
"<label><input type='checkbox' id='cfg'> Configure</label>"
"</div>"
"<div class='grid' id='btns'></div>"
"</div>"

"<div class='card'><h2>OTA Firmware Update</h2>"
"<input type='file' id='bin' accept='.bin'>"
"<button onclick='doOTA()'>Flash Firmware</button>"
"<progress id='prog' value='0' max='100' style='display:none'></progress>"
"<p id='ota-msg' style='margin-top:8px;font-size:.9rem'></p>"
"</div>"

"<script>"
"(function(){"
"var g=document.getElementById('btns');"
"for(var i=1;i<=6;i++){"
"var b=document.createElement('button');"
"b.textContent='Button '+i;"
"b.onclick=(function(n){return function(){btnClick(n);};})(i);"
"g.appendChild(b);"
"}})();"

"function toast(msg,ok){"
"var t=document.getElementById('toast');"
"t.textContent=msg;t.className=ok===false?'err':'ok';t.style.display='block';"
"setTimeout(function(){t.style.display='none';},2500);}"

"async function post(url,data){"
"try{var r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});return r.ok;}"
"catch(e){return false;}}"

"function sendText(){"
"var text=document.getElementById('txt').value;"
"var blink=document.getElementById('blink').checked;"
"if(!text){toast('Enter a message',false);return;}"
"post('/text',{text:text,blink:blink}).then(function(ok){toast(ok?'Sent!':'Failed',ok);});}"

"function sendRaw(){"
"var hex=document.getElementById('txt').value.replace(/[^0-9a-fA-F]/g,'');"
"if(!hex){toast('Enter hex bytes',false);return;}"
"post('/raw',{hex:hex}).then(function(ok){toast(ok?'Sent!':'Failed',ok);});}"

"function btnClick(n){"
"var cfg=document.getElementById('cfg').checked;"
"var cmd=document.getElementById('cmd').value;"
"document.getElementById('cfg').checked=false;"
"var body=cfg?{button:n,config:true,command:cmd}:{button:n};"
"post('/button',body).then(function(ok){toast(ok?(cfg?'Button '+n+' configured':'Button '+n+' triggered'):'Failed',ok);});"
"if(cfg)document.getElementById('cmd').value='';}"

"function doOTA(){"
"var f=document.getElementById('bin').files[0];"
"if(!f){toast('No file selected',false);return;}"
"var prog=document.getElementById('prog');"
"var msg=document.getElementById('ota-msg');"
"var reader=new FileReader();"
"reader.onload=function(e){"
"var buf=e.target.result;"
"prog.style.display='block';prog.value=0;"
"msg.textContent='Uploading '+f.name+'...';"
"var xhr=new XMLHttpRequest();"
"xhr.upload.onprogress=function(ev){if(ev.lengthComputable)prog.value=Math.round(ev.loaded/ev.total*100);};"
"xhr.onload=function(){if(xhr.status===200){msg.textContent='Flash complete! Rebooting...';}else{msg.textContent='OTA failed: '+xhr.responseText;toast('OTA Failed',false);}};"
"xhr.onerror=function(){msg.textContent='Connection error';toast('Error',false);};"
"xhr.open('POST','/ota');"
"xhr.setRequestHeader('Content-Type','application/octet-stream');"
"xhr.send(buf);"
"};reader.readAsArrayBuffer(f);}"
"</script>"
"</body></html>";

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
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
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
/* ------------------------------------------------------------------ */

httpd_handle_t start_http_server(QueueHandle_t display_queue)
{
    s_display_queue = display_queue;
    memset(s_button_cmds, 0, sizeof(s_button_cmds));

    httpd_config_t config      = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers    = 8;
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
    };

    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return server;
}
