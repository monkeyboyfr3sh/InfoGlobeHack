#include "http_fetch.h"

#include <stdlib.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "FETCH";

char *http_fetch(const char *url, size_t max_bytes, size_t *len, int *status)
{
    if (len) *len = 0;
    if (status) *status = 0;

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "infoglobe/1.0",
        .buffer_size = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;

    char *body = NULL;
    size_t got = 0, cap = 0;
    bool ok = false;

    if (esp_http_client_open(c, 0) != ESP_OK) {
        ESP_LOGW(TAG, "Could not connect: %s", url);
        goto done;
    }
    esp_http_client_fetch_headers(c);              // content length, or 0/-1 when chunked
    int code = esp_http_client_get_status_code(c);
    if (status) *status = code;
    if (code < 200 || code > 299) {
        ESP_LOGW(TAG, "HTTP %d: %s", code, url);
        goto done;
    }
    for (;;) {
        if (got + 1 >= cap) {
            size_t want = cap ? cap * 2 : 2048;
            if (want > max_bytes + 1) want = max_bytes + 1;
            if (want <= cap) {                     // body is longer than allowed
                ESP_LOGW(TAG, "Body over %u bytes: %s", (unsigned)max_bytes, url);
                goto done;
            }
            char *grown = realloc(body, want);
            if (!grown) goto done;
            body = grown;
            cap = want;
        }
        int r = esp_http_client_read(c, body + got, cap - got - 1);
        if (r < 0) goto done;
        if (r == 0) break;
        got += r;
    }
    if (!esp_http_client_is_complete_data_received(c)) {
        ESP_LOGW(TAG, "Incomplete body: %s", url);
        goto done;
    }
    body[got] = '\0';
    ok = true;

done:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (!ok) {
        free(body);
        return NULL;
    }
    if (len) *len = got;
    return body;
}
