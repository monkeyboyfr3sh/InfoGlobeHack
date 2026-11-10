/* Weather feed: current weather and the forecast for the saved location from
 * Open-Meteo (free, no key), published as live values: {temp}, {sky},
 * {weather} ("61 CLOUDY"), ... (docs/weather.md has the list).
 *
 * Settings (POST /api/v1/settings): "lat"/"lon" (the same location the clock
 * uses for sunrise), "weather": false turns the feed off (it is on whenever a
 * location is saved), "units": "c" for Celsius and km/h (default: Fahrenheit
 * and mph). One request every 15 minutes, about 1 KB.
 *
 * The parsing half needs only cJSON and libc, so tools/weather-test compiles
 * it on the PC against saved real answers (WEATHER_HOST_TEST). */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#define WX_POLL_S     (15 * 60)          // Open-Meteo's "current" moves in 15-minute steps
#define WX_TTL_S      (3 * WX_POLL_S)    // stale weather disappears instead of lying
#define WX_RETRY_S    60                 // first retry after a failure, doubling up to WX_POLL_S
#define WX_BODY_MAX   4096               // the real answer is ~950 bytes
#define WX_TEXT       24

/* The values, in WX_KEYS order. Only glyphs the globe has: A-Z, 0-9, space, '-'. */
enum { W_TEMP, W_FEELS, W_HI, W_LO, W_HUMID, W_WIND, W_WINDDIR, W_SKY, W_RAIN, W_THI, W_TLO, W_TSKY, W_WEATHER, W_N };
static const char *const WX_KEYS[W_N] = {
    "temp", "feels", "hi", "lo", "humid", "wind", "winddir", "sky", "rain", "thi", "tlo", "tsky", "weather",
};

/* WMO weather code -> a short word that reads from across the room (<= 10 chars). */
static const char *wx_sky(int code, bool day)
{
    switch (code) {
    case 0: case 1:                     return day ? "SUNNY" : "CLEAR";   // clear, mainly clear
    case 2:                             return "PT CLOUDY";
    case 3:                             return "CLOUDY";                  // overcast
    case 45: case 48:                   return "FOG";
    case 51: case 53: case 55:          return "DRIZZLE";
    case 56: case 57: case 66: case 67: return "ICY RAIN";                // freezing drizzle / rain
    case 61: case 63:                   return "RAIN";
    case 65: case 82:                   return "HEAVY RAIN";
    case 80: case 81:                   return "SHOWERS";
    case 71: case 73: case 77: case 86: return "SNOW";
    case 75:                            return "HEAVY SNOW";
    case 85:                            return "FLURRIES";                // light snow showers
    case 95:                            return "STORM";
    case 96: case 99:                   return "HAIL";                    // thunderstorm with hail
    }
    return NULL;
}

static const char *wx_compass(double deg)
{
    static const char *const P[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int i = (int)floor(fmod(deg, 360) / 45 + 0.5) % 8;
    return P[i < 0 ? i + 8 : i];
}

static bool wx_num(const cJSON *j, double *v)
{
    if (!cJSON_IsNumber(j) || !(fabs(j->valuedouble) < 100000)) return false;   // null when a model has no data
    *v = j->valuedouble;
    return true;
}

static void wx_whole(char *out, double v)
{
    snprintf(out, WX_TEXT, "%ld", lround(v));   // lround(-0.4) is 0: no "-0"
}

/* The request: only the fields we use, in the units we show. */
static void wx_url(char *url, size_t cap, double lat, double lon, bool metric)
{
    snprintf(url, cap, "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,"
             "wind_speed_10m,wind_direction_10m,is_day"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&forecast_days=2&timezone=auto%s",
             lat, lon, metric ? "" : "&temperature_unit=fahrenheit&wind_speed_unit=mph");
}

/* Open-Meteo answer -> out[i] for WX_KEYS[i] ("" = not in the answer).
 * False when it isn't a forecast at all (no current temperature). */
static bool wx_parse(const char *json, char out[W_N][WX_TEXT])
{
    for (int i = 0; i < W_N; i++) out[i][0] = '\0';
    cJSON *root = cJSON_Parse(json);
    const cJSON *cur = cJSON_GetObjectItem(root, "current"), *daily = cJSON_GetObjectItem(root, "daily");
    double v;
    if (!wx_num(cJSON_GetObjectItem(cur, "temperature_2m"), &v)) {
        cJSON_Delete(root);
        return false;
    }
    wx_whole(out[W_TEMP], v);
    if (wx_num(cJSON_GetObjectItem(cur, "apparent_temperature"), &v)) wx_whole(out[W_FEELS], v);
    if (wx_num(cJSON_GetObjectItem(cur, "relative_humidity_2m"), &v)) wx_whole(out[W_HUMID], v);
    if (wx_num(cJSON_GetObjectItem(cur, "wind_speed_10m"), &v))       wx_whole(out[W_WIND], v);
    if (wx_num(cJSON_GetObjectItem(cur, "wind_direction_10m"), &v))   snprintf(out[W_WINDDIR], WX_TEXT, "%s", wx_compass(v));
    double day = 1;
    wx_num(cJSON_GetObjectItem(cur, "is_day"), &day);
    const char *sky = wx_num(cJSON_GetObjectItem(cur, "weather_code"), &v) ? wx_sky((int)v, day != 0) : NULL;
    if (sky) snprintf(out[W_SKY], WX_TEXT, "%s", sky);
    snprintf(out[W_WEATHER], WX_TEXT, "%.11s%s%.10s", out[W_TEMP], sky ? " " : "", sky ? sky : "");   // "61 CLOUDY"

    // daily[] starts today (local to the location, timezone=auto); match it to
    // the current date anyway, so an extra past day can't shift it
    const cJSON *now = cJSON_GetObjectItem(cur, "time"), *d;
    int today = 0, i = 0;
    cJSON_ArrayForEach(d, cJSON_GetObjectItem(daily, "time")) {
        if (cJSON_IsString(d) && cJSON_IsString(now) && !strncmp(d->valuestring, now->valuestring, 10)) { today = i; break; }
        i++;
    }
    static const struct { int hi, lo, sky, rain; } DAY[2] = { { W_HI, W_LO, -1, W_RAIN }, { W_THI, W_TLO, W_TSKY, -1 } };
    for (int k = 0; k < 2; k++) {
        int n = today + k;
        if (wx_num(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_max"), n), &v)) wx_whole(out[DAY[k].hi], v);
        if (wx_num(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_min"), n), &v)) wx_whole(out[DAY[k].lo], v);
        if (DAY[k].rain >= 0 && wx_num(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "precipitation_probability_max"), n), &v))
            wx_whole(out[DAY[k].rain], v);
        if (DAY[k].sky >= 0 && wx_num(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "weather_code"), n), &v)
            && (sky = wx_sky((int)v, true)) != NULL)
            snprintf(out[DAY[k].sky], WX_TEXT, "%s", sky);
    }
    cJSON_Delete(root);
    return true;
}

#ifndef WEATHER_HOST_TEST

#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "feeds.h"
#include "http_fetch.h"
#include "scene_engine.h"
#include "value_store.h"

static const char *TAG = "WEATHER";

typedef struct {
    bool   on, have_loc, metric;
    double lat, lon;
} wx_cfg_t;

static bool cfg_coord(const cJSON *s, const char *key, double limit, double *v)
{
    const cJSON *j = cJSON_GetObjectItem(s, key);
    if (!cJSON_IsNumber(j) || fabs(j->valuedouble) > limit) return false;
    *v = j->valuedouble;
    return true;
}

static void read_cfg(wx_cfg_t *c)
{
    for (int attempt = 0;; attempt++) {
        cJSON *s = settings_load();
        const cJSON *on = cJSON_GetObjectItem(s, "weather"), *u = cJSON_GetObjectItem(s, "units");
        memset(c, 0, sizeof(*c));
        c->on = !cJSON_IsFalse(on);
        c->metric = cJSON_IsString(u) && (!strcmp(u->valuestring, "c") || !strcmp(u->valuestring, "C") || !strcmp(u->valuestring, "metric"));
        c->have_loc = cfg_coord(s, "lat", 90, &c->lat) && cfg_coord(s, "lon", 180, &c->lon);
        cJSON_Delete(s);
        if (c->have_loc || attempt) return;
        // settings_save() removes the file before renaming the new one in, so a
        // read can land in that gap and see {}: look again before believing it
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static bool cfg_same(const wx_cfg_t *a, const wx_cfg_t *b)
{
    return a->on == b->on && a->have_loc == b->have_loc && a->metric == b->metric && a->lat == b->lat && a->lon == b->lon;
}

static void clear_values(void)
{
    for (int i = 0; i < W_N; i++) value_store_delete(WX_KEYS[i]);
    value_store_delete("wx_updated");
}

/* wx_status has no TTL: it says why there's no weather (the Data page shows it). */
static void set_status(const char *s) { value_store_set("wx_status", s, 0); }

/* One fetch. Returns true when the values were published. */
static bool fetch_once(const wx_cfg_t *c)
{
    static char url[448];                           // static: the stack is for TLS
    static char vals[W_N][WX_TEXT];
    wx_url(url, sizeof(url), c->lat, c->lon, c->metric);
    int code = 0;
    char *body = http_fetch(url, WX_BODY_MAX, NULL, &code);
    bool got = body != NULL, ok = got && wx_parse(body, vals);
    free(body);
    if (!ok) {
        // no answer at all: network or DNS; 400: Open-Meteo rejected the coordinates
        set_status(!got && code == 0 ? "OFFLINE" : code == 400 ? "BAD LOCATION" : "ERROR");
        ESP_LOGW(TAG, "No weather (HTTP %d%s)", code, got ? ", unreadable answer" : "");
        return false;
    }
    for (int i = 0; i < W_N; i++) {
        if (vals[i][0]) value_store_set(WX_KEYS[i], vals[i], WX_TTL_S);
        else value_store_delete(WX_KEYS[i]);
    }
    char t[24];
    snprintf(t, sizeof(t), "%lld", (long long)time(NULL));
    value_store_set("wx_updated", t, 0);            // unix time of the last good answer
    set_status("OK");
    ESP_LOGI(TAG, "%s, hi %s lo %s (%s; stack left %u B)", vals[W_WEATHER], vals[W_HI], vals[W_LO],
             c->metric ? "C, km/h" : "F, mph", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return true;
}

static void weather_task(void *arg)
{
    (void)arg;
    uint32_t gen = feeds_settings_generation();
    int retry_s = WX_RETRY_S;
    wx_cfg_t cfg;
    read_cfg(&cfg);
    for (;;) {
        int wait_s = WX_POLL_S;                     // 0 = until the settings change
        bool ok = false;
        if (!cfg.on || !cfg.have_loc) {
            clear_values();
            set_status(cfg.on ? "NO LOCATION" : "OFF");
            wait_s = 0;
        } else if ((ok = fetch_once(&cfg))) {
            retry_s = WX_RETRY_S;
        } else {
            wait_s = retry_s;
            retry_s = retry_s * 2 > WX_POLL_S ? WX_POLL_S : retry_s * 2;
        }
        // Wait for the next poll. Any settings save bumps the generation (the
        // aircraft feed's too), so fetch early only when ours changed, or when
        // the last try failed (someone is probably looking at the Data page).
        for (int s = 0; wait_s == 0 || s < wait_s; s++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            uint32_t g = feeds_settings_generation();
            if (g == gen) continue;
            gen = g;
            wx_cfg_t now;
            read_cfg(&now);
            if (cfg_same(&now, &cfg) && ok) continue;
            if (!cfg_same(&now, &cfg)) retry_s = WX_RETRY_S;
            cfg = now;
            break;
        }
    }
}

void feed_weather_start(void)
{
    static bool started;
    if (started) return;
    started = true;
    // low priority: below the display and HTTP server; TLS needs the 8 KB
    if (xTaskCreate(weather_task, "weather", 8192, NULL, 3, NULL) != pdPASS) ESP_LOGE(TAG, "Could not start the weather task");
}

#endif
