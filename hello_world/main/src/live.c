/* Live mirror over WebSocket: GET /api/v1/ws
 *
 * Server -> client, text frames:
 *   {"type":"now","now":{...same as GET /api/v1/now...}}
 *   {"type":"values","values":{...same as GET /api/v1/values...}}
 * Both arrive right after connecting and again whenever they change (the
 * clock's blinking colon changes "now" twice a second, a playlist countdown
 * once a second). "now" is repeated after 15 s without a message, so a client
 * can tell a quiet globe from a dead link. Frames from the client are read and
 * ignored (up to 128 bytes; bigger ones close the connection).
 *
 * Keeping the server safe:
 * - A low-priority watcher task serializes both states 4 times a second while
 *   anyone is connected and compares them with the last copy. It never
 *   touches a socket, so no client can hold it up.
 * - All socket work runs in the HTTP server's task, in one httpd_queue_work()
 *   item per tick; the next is queued only after the last one has run.
 * - Live sessions send with a 300 ms deadline instead of the server's
 *   blocking send (60 s timeout here, and no retry of partial WebSocket
 *   writes). A client that can't take a frame in time is dropped with
 *   shutdown(): the server sees the socket close and removes the session the
 *   normal way, so we never queue a second close that could land on a reused
 *   session slot.
 * - At most LIVE_MAX_CLIENTS; one more gets close code 1013 (try again later).
 *   The server pings every 15 s (browsers answer by themselves); a client
 *   silent for 45 s is dropped.
 * - ESP-IDF 6.1 completes the handshake without calling the handler, so new
 *   connections are found by a scan of the session list on every tick. */

#include "live.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "cJSON.h"

#include "scene_engine.h"
#include "value_store.h"

#if !CONFIG_HTTPD_WS_SUPPORT
#error "The live mirror needs CONFIG_HTTPD_WS_SUPPORT=y (Component config > HTTP Server)"
#endif

static const char *TAG = "LIVE";

#define TICK_MS        250
#define SEND_MS        300      // a whole frame must be handed to TCP within this
#define PING_MS        15000
#define HEARTBEAT_MS   15000    // repeat "now" after this long without a message
#define DEAD_MS        45000    // nothing from the client (pongs included) for this long: drop it
#define REAP_MS        10000    // dropped but still not closed by the server: close it ourselves
#define RX_MAX         128

enum { NOW, VALUES, KINDS };

typedef struct {
    int        fd;                  // -1: free slot
    bool       dead;                // shut down, waiting for the server to remove the session
    uint32_t   sent[KINDS];         // version of each message this client has
    TickType_t last_rx, last_ping, last_text, dead_at;
} client_t;

static httpd_handle_t    s_server;
static TaskHandle_t      s_watcher;
static SemaphoreHandle_t s_lock;                       // s_msg/s_ver: watcher writes, tick() reads
static char             *s_msg[KINDS];
static uint32_t          s_ver[KINDS];
static client_t          s_clients[LIVE_MAX_CLIENTS];  // HTTP server task only
static volatile int      s_nclients;
static volatile bool     s_queued;
static char              s_refused;                    // session context of connections turned away

/* ---- sockets (HTTP server task only) ---- */

/* Send function for live sessions: all of buf within SEND_MS, or an error. */
static int live_send(httpd_handle_t hd, int fd, const char *buf, size_t len, int flags)
{
    (void)hd;
    if (!buf) return HTTPD_SOCK_ERR_INVALID;
    size_t done = 0;
    TickType_t t0 = xTaskGetTickCount();
    while (done < len) {
        int r = send(fd, buf + done, len - done, flags | MSG_DONTWAIT);
        if (r > 0) { done += r; continue; }
        if (r < 0 && errno != EAGAIN && errno != EINTR) return HTTPD_SOCK_ERR_FAIL;
        if (xTaskGetTickCount() - t0 >= pdMS_TO_TICKS(SEND_MS)) return HTTPD_SOCK_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(10));      // send buffer full: give the client a moment
    }
    return (int)done;
}

static bool ws_send(int fd, httpd_ws_type_t type, const void *data, size_t len)
{
    httpd_ws_frame_t f = { .type = type, .payload = (uint8_t *)data, .len = len };
    return httpd_ws_send_frame_async(s_server, fd, &f) == ESP_OK;
}

static bool is_client(const void *p)
{
    return p >= (const void *)s_clients && p < (const void *)(s_clients + LIVE_MAX_CLIENTS);
}

/* The server is removing the session (any reason) and frees its context: our slot. */
static void client_gone(void *ctx)
{
    client_t *c = ctx;
    ESP_LOGI(TAG, "Client %d gone", c->fd);
    c->fd = -1;
    c->dead = false;
    s_nclients--;
}

static void keep_ctx(void *ctx) { (void)ctx; }       // &s_refused is static: nothing to free

static void drop(client_t *c, const char *why, TickType_t t)
{
    ESP_LOGI(TAG, "Dropping client %d: %s", c->fd, why);
    c->dead = true;
    c->dead_at = t;
    shutdown(c->fd, SHUT_RDWR);      // the server notices on its next select() and closes the session
}

/* New WebSocket sessions: adopt them, or turn them away when full. */
static void adopt(TickType_t t)
{
    int fds[CONFIG_LWIP_MAX_SOCKETS];          // >= max_open_sockets (httpd_start checks that)
    size_t n = sizeof(fds) / sizeof(fds[0]);
    if (httpd_get_client_list(s_server, &n, fds) != ESP_OK) return;
    for (size_t i = 0; i < n; i++) {
        int fd = fds[i];
        if (httpd_ws_get_fd_info(s_server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
        if (httpd_sess_get_ctx(s_server, fd)) continue;          // adopted or refused already
        httpd_sess_set_send_override(s_server, fd, live_send);
        client_t *c = NULL;
        for (int k = 0; k < LIVE_MAX_CLIENTS && !c; k++) {
            if (s_clients[k].fd < 0) c = &s_clients[k];
        }
        if (!c) {
            static const uint8_t busy[] = { 0x03, 0xF5, 'b', 'u', 's', 'y' };   // close code 1013
            ESP_LOGW(TAG, "Refusing client %d: %d already connected", fd, LIVE_MAX_CLIENTS);
            httpd_sess_set_ctx(s_server, fd, &s_refused, keep_ctx);
            ws_send(fd, HTTPD_WS_TYPE_CLOSE, busy, sizeof(busy));
            shutdown(fd, SHUT_RDWR);
            continue;
        }
        *c = (client_t){ .fd = fd, .last_rx = t, .last_ping = t, .last_text = t };
        httpd_sess_set_ctx(s_server, fd, c, client_gone);
        s_nclients++;
        ESP_LOGI(TAG, "Client %d connected (%d of %d)", fd, s_nclients, LIVE_MAX_CLIENTS);
        xTaskNotifyGive(s_watcher);                              // build its first messages now
    }
}

static bool due(const client_t *c, int k, const uint32_t *ver, TickType_t t)
{
    return c->sent[k] != ver[k] || (k == NOW && t - c->last_text >= pdMS_TO_TICKS(HEARTBEAT_MS));
}

/* One tick, in the HTTP server's task (queued by the watcher). */
static void tick(void *arg)
{
    (void)arg;
    TickType_t t = xTaskGetTickCount();
    adopt(t);

    // Copy the messages someone needs, so no lock is held while sending.
    char *msg[KINDS] = { NULL };
    uint32_t ver[KINDS];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int k = 0; k < KINDS; k++) {
        ver[k] = s_ver[k];
        bool want = false;
        for (int i = 0; i < LIVE_MAX_CLIENTS; i++) {
            if (s_clients[i].fd >= 0 && !s_clients[i].dead && due(&s_clients[i], k, ver, t)) want = true;
        }
        if (want && s_msg[k]) msg[k] = strdup(s_msg[k]);
    }
    xSemaphoreGive(s_lock);

    for (int i = 0; i < LIVE_MAX_CLIENTS; i++) {
        client_t *c = &s_clients[i];
        if (c->fd < 0) continue;
        if (c->dead) {                  // shutdown() should have closed it by now; make sure
            if (t - c->dead_at >= pdMS_TO_TICKS(REAP_MS)) {
                c->dead_at = t;
                httpd_sess_trigger_close(s_server, c->fd);
            }
            continue;
        }
        if (httpd_ws_get_fd_info(s_server, c->fd) != HTTPD_WS_CLIENT_WEBSOCKET) continue;   // closing
        if (t - c->last_rx >= pdMS_TO_TICKS(DEAD_MS)) { drop(c, "no answer to pings", t); continue; }
        bool ok = true;
        for (int k = 0; k < KINDS && ok; k++) {
            if (!msg[k] || !due(c, k, ver, t)) continue;
            ok = ws_send(c->fd, HTTPD_WS_TYPE_TEXT, msg[k], strlen(msg[k]));
            if (ok) { c->sent[k] = ver[k]; c->last_text = t; }
        }
        if (ok && t - c->last_ping >= pdMS_TO_TICKS(PING_MS)) {
            ok = ws_send(c->fd, HTTPD_WS_TYPE_PING, NULL, 0);
            c->last_ping = t;
        }
        if (!ok) { drop(c, "send failed or too slow", t); continue; }
        // A live mirror shouldn't be the session LRU purging picks: idle HTTP ones go first.
        httpd_sess_update_lru_counter(s_server, c->fd);
    }
    free(msg[NOW]);
    free(msg[VALUES]);
    s_queued = false;
}

/* Frames from a client: read and ignore, but they show it's alive (pongs
 * included). A plain GET without the upgrade gets a 426. */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {     // WebSocket frames come with method 0
        httpd_resp_set_status(req, "426 Upgrade Required");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Connect with a WebSocket: ws://<globe>/api/v1/ws\"}");
        return ESP_OK;
    }
    uint8_t buf[RX_MAX];
    httpd_ws_frame_t f = { .payload = buf };
    if (httpd_ws_recv_frame(req, &f, sizeof(buf)) != ESP_OK) return ESP_FAIL;   // too big or broken: close
    if (is_client(req->sess_ctx)) ((client_t *)req->sess_ctx)->last_rx = xTaskGetTickCount();
    return ESP_OK;
}

/* ---- watcher (own task, never touches sockets) ---- */

static char *message(const char *type, cJSON *payload)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", type);
    cJSON_AddItemToObject(o, type, payload);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

static void update(int k, char *fresh)
{
    if (!fresh) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_msg[k] || strcmp(s_msg[k], fresh)) {
        free(s_msg[k]);
        s_msg[k] = fresh;
        s_ver[k]++;
        fresh = NULL;
    }
    xSemaphoreGive(s_lock);
    free(fresh);
}

static void watcher_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TICK_MS));     // early when a client was just adopted
        if (s_nclients > 0) {
            update(NOW, message("now", scene_engine_status()));
            update(VALUES, message("values", value_store_json()));
        } else if (s_msg[NOW] || s_msg[VALUES]) {
            // Nobody listening: forget, so a newcomer never gets an old state.
            xSemaphoreTake(s_lock, portMAX_DELAY);
            for (int k = 0; k < KINDS; k++) { free(s_msg[k]); s_msg[k] = NULL; }
            xSemaphoreGive(s_lock);
        }
        if (!s_queued) {
            s_queued = true;
            if (httpd_queue_work(s_server, tick, NULL) != ESP_OK) s_queued = false;   // server busy: next tick
        }
    }
}

void live_register(httpd_handle_t server)
{
    s_server = server;
    for (int i = 0; i < LIVE_MAX_CLIENTS; i++) s_clients[i].fd = -1;
    s_lock = xSemaphoreCreateMutex();
    static const httpd_uri_t ws = {
        .uri = "/api/v1/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true,
    };
    if (!s_lock || httpd_register_uri_handler(server, &ws) != ESP_OK) {
        ESP_LOGE(TAG, "Could not register /api/v1/ws");
        return;
    }
    xTaskCreate(watcher_task, "live", 3584, NULL, tskIDLE_PRIORITY + 2, &s_watcher);
}
