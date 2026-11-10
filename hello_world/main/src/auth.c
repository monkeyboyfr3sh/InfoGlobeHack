/* Access token for write requests (see auth.h). */

#include "auth.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "AUTH";

#define NVS_NS   "auth"
#define NVS_KEY  "token"

/* NUL-padded to the full size, so a compare can always walk every byte. */
static char s_token[AUTH_TOKEN_MAX + 1];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;   // the button task clears it too

static void set_ram(const char *t)
{
    char pad[sizeof(s_token)] = { 0 };
    strncpy(pad, t, AUTH_TOKEN_MAX);
    portENTER_CRITICAL(&s_mux);
    memcpy(s_token, pad, sizeof(s_token));
    portEXIT_CRITICAL(&s_mux);
}

bool auth_token_valid(const char *t)
{
    size_t n = strlen(t);
    if (n == 0) return true;
    if (n < AUTH_TOKEN_MIN || n > AUTH_TOKEN_MAX) return false;
    for (const char *p = t; *p; p++) {
        char c = *p;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '~' || c == '-';
        if (!ok) return false;
    }
    return true;
}

void auth_init(void)
{
    char t[AUTH_TOKEN_MAX + 1] = "";
    size_t n = sizeof(t);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {         // missing namespace = no token
        if (nvs_get_str(h, NVS_KEY, t, &n) != ESP_OK || !auth_token_valid(t)) t[0] = '\0';
        nvs_close(h);
    }
    set_ram(t);
    ESP_LOGI(TAG, "%s", t[0] ? "Access token set: changes need it"
                             : "No access token: anyone on the network can make changes");
}

bool auth_required(void)
{
    portENTER_CRITICAL(&s_mux);
    bool r = s_token[0] != '\0';
    portEXIT_CRITICAL(&s_mux);
    return r;
}

bool auth_set(const char *token)
{
    if (!token || !auth_token_valid(token)) return false;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        if (token[0]) e = nvs_set_str(h, NVS_KEY, token);
        else if ((e = nvs_erase_key(h, NVS_KEY)) == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "Could not save the token: %s", esp_err_to_name(e));
        return false;
    }
    set_ram(token);
    ESP_LOGW(TAG, "%s", token[0] ? "Access token changed" : "Access token removed");
    return true;
}

void auth_clear(void)
{
    if (!auth_set("")) set_ram("");     // NVS trouble: at least open up until the next reboot
}

/* ---- checking requests ---- */

/* Copies a candidate token, minus surrounding blanks. false if it's too long to be one. */
static bool take(const char *s, char *out)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (n > AUTH_TOKEN_MAX) return false;
    memcpy(out, s, n);
    return true;
}

/* The token sent with the request into `out` (zeroed, AUTH_TOKEN_MAX + 1 bytes).
 * 1 = found, 0 = none sent, -1 = sent but can't be a token. */
static int given(httpd_req_t *req, char *out)
{
    char hdr[AUTH_TOKEN_MAX + 24];
    size_t n = httpd_req_get_hdr_value_len(req, "Authorization");
    if (n && n < sizeof(hdr) && httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK &&
        !strncasecmp(hdr, "Bearer ", 7)) {
        return take(hdr + 7, out) ? 1 : -1;
    }                                       // other schemes (a proxy's Basic auth) aren't ours: look further
    n = httpd_req_get_hdr_value_len(req, "X-Globe-Token");
    if (n) {
        if (n >= sizeof(hdr) || httpd_req_get_hdr_value_str(req, "X-Globe-Token", hdr, sizeof(hdr)) != ESP_OK) return -1;
        return take(hdr, out) ? 1 : -1;
    }
    size_t qn = httpd_req_get_url_query_len(req);
    if (!qn) return 0;
    char *q = malloc(qn + 1);
    if (!q) return -1;
    int r = 0;
    if (httpd_req_get_url_query_str(req, q, qn + 1) == ESP_OK) {
        esp_err_t e = httpd_query_key_value(q, "token", out, AUTH_TOKEN_MAX + 1);
        r = e == ESP_OK ? 1 : e == ESP_ERR_NOT_FOUND ? 0 : -1;
        if (r < 0) memset(out, 0, AUTH_TOKEN_MAX + 1);
    }
    free(q);
    return r;
}

/* Constant time: both sides are NUL-padded to the same size and every byte
 * is compared, so the time doesn't depend on how much of a guess is right. */
static bool matches(const char *cand)
{
    char want[sizeof(s_token)];
    portENTER_CRITICAL(&s_mux);
    memcpy(want, s_token, sizeof(want));
    portEXIT_CRITICAL(&s_mux);
    uint8_t diff = 0;
    for (size_t i = 0; i < sizeof(want); i++) diff |= (uint8_t)(want[i] ^ cand[i]);
    bool ok = diff == 0 && want[0] != '\0';
    memset(want, 0, sizeof(want));
    return ok;
}

/* -1: not required, 0: refused (no token sent), 1: accepted, 2: refused (wrong token) */
static int verdict(httpd_req_t *req)
{
    if (!auth_required()) return -1;
    char cand[AUTH_TOKEN_MAX + 1] = { 0 };
    int g = given(req, cand);
    if (g == 0) return 0;
    bool ok = g > 0 && matches(cand);
    memset(cand, 0, sizeof(cand));
    return ok ? 1 : 2;
}

bool auth_request_ok(httpd_req_t *req)
{
    int v = verdict(req);
    return v == -1 || v == 1;
}

bool auth_check(httpd_req_t *req)
{
    int v = verdict(req);
    if (v == -1 || v == 1) return true;

    const char *msg = v == 0
        ? "This globe needs its access token for changes (Authorization: Bearer <token>, X-Globe-Token or ?token=)"
        : "Wrong access token";
    // No query string in the log: it may hold a (wrong) token.
    ESP_LOGW(TAG, "401 %.*s: %s", (int)strcspn(req->uri, "?"), req->uri, v == 0 ? "no token" : "wrong token");

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer realm=\"InfoGlobe\"");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", AUTH_CORS_HEADERS);
    char body[192];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", msg);
    httpd_resp_sendstr(req, body);
    return false;
}
