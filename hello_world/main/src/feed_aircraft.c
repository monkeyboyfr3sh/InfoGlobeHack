/* Aircraft overhead: the aircraft nearest to the globe's location as live
 * values ({plane}, {planeline}, {planewhere}, ...; see docs/aircraft.md).
 *
 * With "aircraft": true in the settings (off by default: it polls a community
 * service) a task asks a free ADS-B aggregator every ~15 s for the aircraft
 * within a few nautical miles: adsb.lol, or adsb.fi when adsb.lol fails. Both
 * answer in readsb's JSON format. The nearest flight's route comes from
 * adsbdb.com, once per callsign (cached), and is only shown when the aircraft
 * is plausibly flying it. When a new aircraft comes within the alert distance
 * while the playlist owns the display, the "plane" scene plays once.
 *
 * The parsing, maths and value making build on a PC as well (no ESP-IDF):
 * hello_world/host_test/feed_aircraft_test.c includes this file with
 * AIRCRAFT_HOST_TEST defined. */

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "value_store.h"

#ifndef AIRCRAFT_HOST_TEST
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "feeds.h"
#include "http_fetch.h"
#include "scene_engine.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EARTH_NM     3440.065       // mean Earth radius (6371 km), as readsb uses
#define FRAME_CHARS  35             // one frame of text on the globe
#define CITY_MAX     20
#define STALE_S      60             // ignore positions older than this
#define STICKY_NM    0.5            // keep showing an aircraft unless another is this much closer
#define OVERHEAD_NM  0.5
#define NEAR_AP_NM   40             // "at" an airport: arriving or departing

/* ------------------------------------------------------------------ */
/*  Maths and text                                                      */
/* ------------------------------------------------------------------ */

static double rad(double d) { return d * (M_PI / 180.0); }

/* Great-circle distance (haversine). */
static double dist_nm(double lat1, double lon1, double lat2, double lon2)
{
    double s1 = sin(rad(lat2 - lat1) / 2), s2 = sin(rad(lon2 - lon1) / 2);
    double a = s1 * s1 + cos(rad(lat1)) * cos(rad(lat2)) * s2 * s2;
    return 2 * EARTH_NM * asin(sqrt(a < 1 ? a : 1));
}

/* Initial course from 1 to 2, degrees clockwise from north. */
static double bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double y = sin(rad(lon2 - lon1)) * cos(rad(lat2));
    double x = cos(rad(lat1)) * sin(rad(lat2)) - sin(rad(lat1)) * cos(rad(lat2)) * cos(rad(lon2 - lon1));
    double b = atan2(y, x) * (180.0 / M_PI);
    return b < 0 ? b + 360 : b;
}

static const char *compass8(double deg)
{
    static const char *const P[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    return P[(int)floor(fmod(deg + 22.5 + 720.0, 360.0) / 45.0) & 7];
}

static double angle_diff(double a, double b) { return fabs(fmod(a - b + 540.0, 360.0) - 180.0); }

/* UTF-8 -> text the globe can draw, in capitals (they read best): accents
 * dropped (Zurich, Lodz, Sao Paulo), characters the font lacks become spaces,
 * runs of spaces collapse. */
static void font_text(char *out, size_t cap, const char *in)
{
    static const char L1[] = "AAAAAAACEEEEIIII" "DNOOOOO OUUUUYTS" "AAAAAAACEEEEIIII" "DNOOOOO OUUUUYTY";  // U+00C0-00FF
    static const char LA[] = "AAAAAACCCCCCCCDD" "DDEEEEEEEEEEGGGG" "GGGGHHHHIIIIIIII" "IIIIJJKKKLLLLLLL"   // U+0100-017F
                             "LLLNNNNNNNNNOOOO" "OOOORRRRRRSSSSSS" "SSTTTTTTUUUUUUUU" "UUUUWWYYYZZZZZZS";
    _Static_assert(sizeof(L1) == 65 && sizeof(LA) == 129, "one letter per code point");
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)(in ? in : "");
    while (*p && o + 1 < cap) {
        unsigned c = *p++;
        if (c >= 0x80) {                                   // 2-byte sequences only; the rest is blank
            unsigned cp = 0;
            if ((c & 0xE0) == 0xC0 && (*p & 0xC0) == 0x80) cp = ((c & 0x1F) << 6) | (*p++ & 0x3F);
            else while ((*p & 0xC0) == 0x80) p++;
            c = cp >= 0xC0 && cp < 0x100 ? (unsigned char)L1[cp - 0xC0]
              : cp >= 0x100 && cp < 0x180 ? (unsigned char)LA[cp - 0x100] : ' ';
        }
        c = (unsigned)toupper((int)c);
        if (c < 0x20 || c > 0x7E || strchr("%&+;@[\\]^_`{|}~", (int)c)) c = ' ';
        if (c == ' ' && (o == 0 || out[o - 1] == ' ')) continue;
        out[o++] = (char)c;
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Aircraft list -> nearest airborne aircraft                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char   hex[8];          // ICAO address, lowercase; "~" in front = not an ICAO address
    char   call[9];         // callsign ("" = none)
    char   reg[12];         // registration
    char   type[5];         // ICAO type designator (B738)
    int    alt_ft;          // barometric (geometric when that's all there is)
    int    vrate;           // feet per minute, INT_MIN = unknown
    int    gs_kt;           // ground speed, -1 = unknown
    int    track;           // degrees, -1 = unknown
    double lat, lon;
    double dist_nm, brg;    // from the globe's location
} ac_t;

typedef struct {
    int  count;             // airborne aircraft within the radius
    bool have;              // `near` is valid
    ac_t near;
} scan_t;

static const cJSON *item(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k); }

static double numval(const cJSON *o, const char *k, double def)
{
    const cJSON *v = item(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static void strval(const cJSON *o, const char *k, char *out, size_t cap)
{
    const cJSON *v = item(o, k);
    font_text(out, cap, cJSON_IsString(v) ? v->valuestring : "");
}

/* One entry of the aircraft array. false = leave it out: on the ground, an
 * airport vehicle, no recent position, or no altitude to tell. */
static bool ac_parse(const cJSON *o, double hlat, double hlon, ac_t *a)
{
    const cJSON *lat = item(o, "lat"), *lon = item(o, "lon"), *cat = item(o, "category"), *hex = item(o, "hex");
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) return false;      // position too old: "lastPosition" only
    if (numval(o, "seen_pos", 0) > STALE_S) return false;
    if (cJSON_IsString(cat) && cat->valuestring[0] == 'C') return false;  // C1-C7: vehicles, obstacles
    const cJSON *alt = item(o, "alt_baro");
    if (cJSON_IsString(alt) && !strcmp(alt->valuestring, "ground")) return false;
    if (!cJSON_IsNumber(alt)) alt = item(o, "alt_geom");
    if (!cJSON_IsNumber(alt)) return false;
    memset(a, 0, sizeof(*a));
    snprintf(a->hex, sizeof(a->hex), "%s", cJSON_IsString(hex) ? hex->valuestring : "");
    for (char *c = a->hex; *c; c++) *c = (char)tolower((unsigned char)*c);
    strval(o, "flight", a->call, sizeof(a->call));
    strval(o, "r", a->reg, sizeof(a->reg));
    strval(o, "t", a->type, sizeof(a->type));
    a->alt_ft = (int)lround(alt->valuedouble);
    double v = numval(o, "baro_rate", NAN);
    if (isnan(v)) v = numval(o, "geom_rate", NAN);
    a->vrate = isnan(v) ? INT_MIN : (int)lround(v);
    v = numval(o, "gs", -1);
    a->gs_kt = v < 0 ? -1 : (int)lround(v);
    v = numval(o, "track", NAN);
    if (isnan(v)) v = numval(o, "true_heading", NAN);
    if (isnan(v)) v = numval(o, "mag_heading", NAN);
    a->track = isnan(v) ? -1 : (int)lround(fmod(v + 360.0, 360.0)) % 360;
    a->lat = lat->valuedouble;
    a->lon = lon->valuedouble;
    a->dist_nm = dist_nm(hlat, hlon, a->lat, a->lon);
    a->brg = bearing_deg(hlat, hlon, a->lat, a->lon);
    return true;
}

/* Start of the aircraft array: "ac" (adsb.lol, adsb.fi v3) or "aircraft" (readsb, adsb.fi v2). */
static const char *find_array(const char *body)
{
    static const char *const keys[] = { "\"ac\"", "\"aircraft\"" };
    for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        for (const char *p = strstr(body, keys[k]); p; p = strstr(p + 1, keys[k])) {
            const char *q = p + strlen(keys[k]);
            while (isspace((unsigned char)*q)) q++;
            if (*q++ != ':') continue;
            while (isspace((unsigned char)*q)) q++;
            if (*q == '[') return q + 1;
        }
    }
    return NULL;
}

/* p at '{': just past the matching '}', or NULL when the text ends first. */
static const char *object_end(const char *p, const char *end)
{
    int depth = 0;
    bool in_str = false;
    for (; p < end; p++) {
        if (in_str) {
            if (*p == '\\') p++;
            else if (*p == '"') in_str = false;
        } else if (*p == '"') {
            in_str = true;
        } else if (*p == '{' || *p == '[') {
            depth++;
        } else if ((*p == '}' || *p == ']') && --depth == 0) {
            return p + 1;
        }
    }
    return NULL;
}

/* Count the airborne aircraft within radius_nm of the location and pick the
 * nearest. Parses one aircraft at a time instead of the whole answer: a busy
 * sky is ~450 bytes per aircraft and a full cJSON tree is several times that.
 * `sticky` (hex of the aircraft shown now) stays the pick unless another one
 * is STICKY_NM closer, so two aircraft at similar distances don't make the
 * globe flip between them every poll. false = not an aircraft list (error
 * page, cut off). */
static bool ac_scan(const char *body, size_t len, double hlat, double hlon, double radius_nm,
                    const char *sticky, scan_t *out)
{
    memset(out, 0, sizeof(*out));
    const char *end = body + len, *p = find_array(body);
    if (!p) return false;
    ac_t best = {0}, kept = {0};
    bool have_kept = false;
    for (;;) {
        while (p < end && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (p >= end || (*p != '{' && *p != ']')) return false;
        if (*p == ']') break;
        const char *q = object_end(p, end);
        if (!q) return false;
        cJSON *o = cJSON_ParseWithLength(p, (size_t)(q - p));
        ac_t a;
        if (o && ac_parse(o, hlat, hlon, &a) && a.dist_nm <= radius_nm) {
            if (!out->count++ || a.dist_nm < best.dist_nm) best = a;
            if (sticky && sticky[0] && !strcmp(a.hex, sticky)) { kept = a; have_kept = true; }
        }
        cJSON_Delete(o);
        p = q;
    }
    if (have_kept && kept.dist_nm <= best.dist_nm + STICKY_NM) best = kept;
    out->have = out->count > 0;
    out->near = best;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Routes (adsbdb.com)                                                 */
/* ------------------------------------------------------------------ */

typedef struct { char code[5]; char city[CITY_MAX + 1]; double lat, lon; } airport_t;
typedef struct { airport_t ap[3]; int n; } route_t;        // origin, [stop], destination

/* Airline flights: 3-letter operator and a flight number (UAL123, BAW9KN).
 * Registrations used as callsigns (N123AB, GCDEF) have no route to look up. */
static bool airline_callsign(const ac_t *a)
{
    const char *c = a->call;
    return strlen(c) >= 4 && isalpha((unsigned char)c[0]) && isalpha((unsigned char)c[1]) &&
           isalpha((unsigned char)c[2]) && isdigit((unsigned char)c[3]) && strcmp(c, a->reg);
}

static bool airport_parse(const cJSON *o, airport_t *ap)
{
    const cJSON *lat = item(o, "latitude"), *lon = item(o, "longitude"), *iata = item(o, "iata_code"), *icao = item(o, "icao_code");
    const char *code = cJSON_IsString(iata) && iata->valuestring[0] ? iata->valuestring : cJSON_IsString(icao) ? icao->valuestring : "";
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) return false;
    font_text(ap->code, sizeof(ap->code), code);
    char city[VALUE_TEXT_MAX];
    strval(o, "municipality", city, sizeof(city));
    size_t n = strlen(city);
    if (n > CITY_MAX) {                                    // too long: cut at a word
        n = CITY_MAX;
        for (size_t i = CITY_MAX; i > 0; i--) if (city[i] == ' ') { n = i; break; }
    }
    memcpy(ap->city, city, n);
    ap->city[n] = '\0';
    ap->lat = lat->valuedouble;
    ap->lon = lon->valuedouble;
    return ap->code[0] != '\0';
}

/* adsbdb's /v0/callsign answer -> route. false = no route in it. */
static bool route_parse(const char *body, route_t *r)
{
    memset(r, 0, sizeof(*r));
    cJSON *j = cJSON_Parse(body);
    const cJSON *fr = item(item(j, "response"), "flightroute");
    bool ok = cJSON_IsObject(fr) && airport_parse(item(fr, "origin"), &r->ap[0]);
    if (ok) {
        r->n = airport_parse(item(fr, "midpoint"), &r->ap[1]) ? 2 : 1;
        ok = airport_parse(item(fr, "destination"), &r->ap[r->n]);
        r->n = ok ? r->n + 1 : 0;
    }
    cJSON_Delete(j);
    return ok;
}

/* Route databases list a callsign's usual route, which is often another leg
 * or plain wrong (around SFO most of them were). Use a leg only when the
 * aircraft is plausibly flying it now: roughly on the way, heading for the
 * destination when en route, not landing at the origin nor climbing out of
 * the destination. Returns the index of the leg's first airport, or -1. */
static int route_leg(const route_t *r, const ac_t *a)
{
    bool low = a->alt_ft < 12000, known = a->vrate != INT_MIN;
    for (int i = 0; i + 1 < r->n; i++) {
        const airport_t *o = &r->ap[i], *d = &r->ap[i + 1];
        double direct = dist_nm(o->lat, o->lon, d->lat, d->lon);
        double from = dist_nm(o->lat, o->lon, a->lat, a->lon), to = dist_nm(a->lat, a->lon, d->lat, d->lon);
        if (direct < 20) continue;                                       // round trip, training flight
        if (from + to > direct * 1.25 + 60) continue;                    // nowhere near the way
        if (from > 60 && to > 60 && a->track >= 0 &&
            angle_diff(a->track, bearing_deg(a->lat, a->lon, d->lat, d->lon)) > 100) continue;
        if (from < NEAR_AP_NM && to > 100 && low && known && a->vrate <= -400) continue;
        if (to < NEAR_AP_NM && from > 100 && low && known && a->vrate >= 400) continue;
        return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Values                                                              */
/* ------------------------------------------------------------------ */

enum { V_PLANE, V_TYPE, V_ALT, V_SPD, V_DIST, V_DIR, V_HDG, V_FROM, V_TO, V_ROUTE, V_TRIP, V_COUNT, V_LINE, V_WHERE, V_N };
static const char *const KEYS[V_N] = {
    "plane", "planetype", "planealt", "planespd", "planedist", "planedir", "planehdg",
    "planefrom", "planeto", "planeroute", "planetrip", "planes", "planeline", "planewhere",
};
typedef struct { char v[V_N][VALUE_TEXT_MAX]; } values_t;

#define SETV(i, ...) snprintf(out->v[i], VALUE_TEXT_MAX, __VA_ARGS__)

/* Every value "--"; the composed line carries the message. */
static void values_none(values_t *out, const char *line)
{
    for (int i = 0; i < V_N; i++) SETV(i, "--");
    SETV(V_LINE, "%s", line);
}

/* " word" onto a frame-sized line when it fits; unknown ("--") parts are left out. */
static void line_add(char *line, const char *word)
{
    size_t n = strlen(line), w = strlen(word);
    if (!w || !strcmp(word, "--") || n + 1 + w > FRAME_CHARS) return;
    line[n] = ' ';
    memcpy(line + n + 1, word, w + 1);
}

/* What a poll found -> the values. a = the nearest aircraft (NULL = none in
 * range), r = its callsign's route (NULL = none known). */
static void values_make(values_t *out, const ac_t *a, int count, const route_t *r, double hlat, double hlon, int radius_nm)
{
    values_none(out, "NO PLANES");
    SETV(V_COUNT, "%d", count);
    if (!a) {
        SETV(V_TRIP, "QUIET SKIES");
        SETV(V_WHERE, "NONE WITHIN %d NM", radius_nm);
        return;
    }
    char who[16];
    font_text(who, sizeof(who), a->call[0] ? a->call : a->reg[0] ? a->reg : a->hex + (a->hex[0] == '~'));
    SETV(V_PLANE, "%s", who);
    if (a->type[0]) SETV(V_TYPE, "%s", a->type);
    int alt = (int)lround(a->alt_ft / 100.0) * 100;
    SETV(V_ALT, "%d", alt);
    if (a->gs_kt >= 0) SETV(V_SPD, "%d", a->gs_kt);
    SETV(V_DIST, "%.1f", a->dist_nm);
    SETV(V_DIR, "%s", compass8(a->brg));
    if (a->track >= 0) SETV(V_HDG, "%s", compass8(a->track));
    int leg = r ? route_leg(r, a) : -1;
    if (leg >= 0) {
        const airport_t *o = &r->ap[leg], *d = &r->ap[leg + 1];
        SETV(V_FROM, "%s", o->code);
        SETV(V_TO, "%s", d->code);
        SETV(V_ROUTE, "%s-%s", o->code, d->code);
        // the far end is the interesting one: landing nearby -> where it came from
        if (dist_nm(hlat, hlon, d->lat, d->lon) <= NEAR_AP_NM) SETV(V_TRIP, "FROM %s", o->city[0] ? o->city : o->code);
        else SETV(V_TRIP, "TO %s", d->city[0] ? d->city : d->code);
    } else if (a->track >= 0) {
        SETV(V_TRIP, "HEADING %s", compass8(a->track));
    }
    char line[FRAME_CHARS + 1];
    snprintf(line, sizeof(line), "%s", who);
    line_add(line, out->v[V_ROUTE]);
    line_add(line, out->v[V_TYPE]);
    SETV(V_LINE, "%s", line);
    if (a->dist_nm < OVERHEAD_NM) SETV(V_WHERE, "OVERHEAD %d FT", alt > 0 ? alt : 0);
    else SETV(V_WHERE, "LOOK %s %.1f NM %d FT", compass8(a->brg), a->dist_nm, alt > 0 ? alt : 0);
}

#undef SETV

/* ------------------------------------------------------------------ */
/*  The feed task                                                       */
/* ------------------------------------------------------------------ */

#ifndef AIRCRAFT_HOST_TEST

static const char *TAG = "AIRCRAFT";

#define BODY_MAX        (128 * 1024)   // ~280 aircraft; busy 25 nm circles measured 26-50 KB
#define ROUTE_BODY_MAX  4096
#define POLL_DEFAULT_S  15
#define POLL_MIN_S      10             // a community service: don't hammer it
#define MIN_US(m)       ((int64_t)(m) * 60 * 1000000)
#define ROUTE_SLOTS     12
#define ALERTED_SLOTS   16

extern volatile bool g_time_on;        // frame_maker_task.c: the playlist owns the display

typedef struct {
    bool   on, have_loc;
    double lat, lon;
    int    radius_nm;       // "aircraftRadius", 2-25, default 10
    double alert_nm;        // "aircraftAlert", 0 = never, default 3
    int    alert_ft;        // "aircraftAlertFt": alert only below this altitude, 0 = any
    int    poll_s;          // "aircraftPoll", seconds, at least 10, default 15
} cfg_t;

/* The location goes out rounded to 0.01 degree (about 1 km); the circle asked
 * for is 1 nm larger to make up for it, and distances are measured from the
 * exact location. */
static const struct { const char *name, *fmt; } PROVIDERS[] = {
    { "adsb.lol", "https://api.adsb.lol/v2/point/%.2f/%.2f/%d" },
    { "adsb.fi",  "https://opendata.adsb.fi/api/v3/lat/%.2f/lon/%.2f/dist/%d" },
};

static int     s_first;                 // provider to ask first
static int64_t s_first_until;           // ... until then, then adsb.lol again
static int     s_radius;                // circle asked for: shrinks while answers are too big
static char    s_shown[8];              // hex of the aircraft shown now
static int64_t s_alert_busy_until;      // a plane alert may still be playing
static struct { char call[9]; bool ok; route_t r; int64_t until; } s_routes[ROUTE_SLOTS];
static struct { char hex[8]; int64_t at; } s_alerted[ALERTED_SLOTS];

static double setting(const cJSON *s, const char *k, double def, double lo, double hi)
{
    const cJSON *v = cJSON_GetObjectItem(s, k);
    double x = cJSON_IsNumber(v) ? v->valuedouble : def;
    return x < lo ? lo : x > hi ? hi : x;
}

static void read_cfg(cfg_t *c)
{
    cJSON *s = settings_load();
    if (!cJSON_IsNumber(cJSON_GetObjectItem(s, "lat"))) {   // may have caught the file mid-rewrite: look again
        cJSON_Delete(s);
        vTaskDelay(pdMS_TO_TICKS(2000));
        s = settings_load();
    }
    const cJSON *lat = cJSON_GetObjectItem(s, "lat"), *lon = cJSON_GetObjectItem(s, "lon");
    memset(c, 0, sizeof(*c));
    c->on = cJSON_IsTrue(cJSON_GetObjectItem(s, "aircraft"));
    c->have_loc = cJSON_IsNumber(lat) && cJSON_IsNumber(lon) && fabs(lat->valuedouble) <= 90 && fabs(lon->valuedouble) <= 180;
    if (c->have_loc) { c->lat = lat->valuedouble; c->lon = lon->valuedouble; }
    c->radius_nm = (int)setting(s, "aircraftRadius", 10, 2, 25);
    c->alert_nm = setting(s, "aircraftAlert", 3, 0, c->radius_nm);
    c->alert_ft = (int)setting(s, "aircraftAlertFt", 0, 0, 60000);
    c->poll_s = (int)setting(s, "aircraftPoll", POLL_DEFAULT_S, POLL_MIN_S, 300);
    cJSON_Delete(s);
}

static bool cfg_same(const cfg_t *a, const cfg_t *b)
{
    return a->on == b->on && a->have_loc == b->have_loc && a->lat == b->lat && a->lon == b->lon &&
           a->radius_nm == b->radius_nm && a->alert_nm == b->alert_nm && a->alert_ft == b->alert_ft && a->poll_s == b->poll_s;
}

static void publish(const values_t *v, uint32_t ttl_s)
{
    for (int i = 0; i < V_N; i++) value_store_set(KEYS[i], v->v[i], ttl_s);
}

enum { POLL_OK, POLL_BIG, POLL_FAIL };

/* Ask the providers in turn for the aircraft around the location. */
static int fetch_list(const cfg_t *c, scan_t *out, size_t *got)
{
    int64_t now = esp_timer_get_time();
    if (s_first && now >= s_first_until) s_first = 0;
    for (int k = 0; k < 2; k++) {
        int i = (s_first + k) % 2, status = 0;
        char url[112];
        snprintf(url, sizeof(url), PROVIDERS[i].fmt, c->lat, c->lon, s_radius + 1);
        size_t len = 0;
        char *body = http_fetch(url, BODY_MAX, &len, &status);
        if (!body && status >= 200 && status < 300) return POLL_BIG;   // over BODY_MAX (or cut off)
        bool answered = body != NULL;
        bool ok = answered && ac_scan(body, len, c->lat, c->lon, s_radius, s_shown, out);
        free(body);
        if (ok) {
            if (i != s_first) {
                s_first = i;
                s_first_until = now + MIN_US(10);
                ESP_LOGW(TAG, "Using %s for now", PROVIDERS[i].name);
            }
            *got = len;
            return POLL_OK;
        }
        ESP_LOGW(TAG, "%s: %s (HTTP %d)", PROVIDERS[i].name, answered ? "not an aircraft list" : "no answer", status);
    }
    return POLL_FAIL;
}

/* The callsign's route: from the cache, else one lookup. Called only for the
 * nearest aircraft, so a lookup happens when that changes (or its cached
 * answer runs out), never on every poll. */
static const route_t *route_for(const ac_t *a)
{
    if (!airline_callsign(a)) return NULL;
    int64_t now = esp_timer_get_time();
    int slot = -1, oldest = 0;
    for (int i = 0; i < ROUTE_SLOTS; i++) {
        if (!strcmp(s_routes[i].call, a->call)) { slot = i; break; }
        if (s_routes[i].until < s_routes[oldest].until) oldest = i;
    }
    if (slot >= 0 && now < s_routes[slot].until) return s_routes[slot].ok ? &s_routes[slot].r : NULL;
    if (slot < 0) slot = oldest;
    char url[64];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", a->call);
    int status = 0;
    char *body = http_fetch(url, ROUTE_BODY_MAX, NULL, &status);
    snprintf(s_routes[slot].call, sizeof(s_routes[slot].call), "%s", a->call);
    s_routes[slot].ok = body && route_parse(body, &s_routes[slot].r);
    free(body);
    // "unknown callsign" (404) is an answer too; no answer at all: ask again in a while
    bool answered = status == 404 || (status >= 200 && status < 300);
    s_routes[slot].until = now + (answered ? MIN_US(6 * 60) : MIN_US(5));
    if (s_routes[slot].ok) {
        const route_t *r = &s_routes[slot].r;
        ESP_LOGI(TAG, "%s: listed as %s-%s%s", a->call, r->ap[0].code, r->ap[r->n - 1].code, r->n > 2 ? " (one stop)" : "");
    } else {
        ESP_LOGI(TAG, "%s: %s", a->call, answered ? "no route listed" : "route lookup failed");
    }
    return s_routes[slot].ok ? &s_routes[slot].r : NULL;
}

/* Seconds one pass of a scene takes, counted as scene_engine_tick does (5 s
 * for a frame without a time). */
static unsigned scene_seconds(const char *id)
{
    cJSON *j = scene_load(id), *f;
    unsigned ms = 0;
    cJSON_ArrayForEach(f, cJSON_GetObjectItem(j, "frames")) {
        cJSON *s = cJSON_GetObjectItem(f, "seconds");
        ms += cJSON_IsNumber(s) && s->valuedouble > 0 ? (unsigned)(s->valuedouble * 1000) : 5000;
    }
    cJSON_Delete(j);
    return (ms + 999) / 1000;
}

/* A new aircraft within the alert distance: play the "plane" scene once over
 * the playlist. Never over text someone put up, a chime or another "play now"
 * scene (it tries again next poll), never twice in 10 minutes for the same
 * aircraft, never while a plane alert may still be playing. */
static void maybe_alert(const cfg_t *c, const ac_t *a)
{
    if (c->alert_nm <= 0 || a->dist_nm > c->alert_nm) return;
    if (c->alert_ft > 0 && a->alt_ft > c->alert_ft) return;
    int64_t now = esp_timer_get_time();
    if (now < s_alert_busy_until || !g_time_on) return;
    int slot = 0;
    for (int i = 0; i < ALERTED_SLOTS; i++) {
        if (!strcmp(s_alerted[i].hex, a->hex) && now - s_alerted[i].at < MIN_US(10)) return;
        if (s_alerted[i].at < s_alerted[slot].at) slot = i;
    }
    cJSON *st = scene_engine_status();
    const cJSON *mode = cJSON_GetObjectItem(st, "mode");
    bool playlist = cJSON_IsString(mode) && !strcmp(mode->valuestring, "playlist");
    cJSON_Delete(st);
    if (!playlist) return;
    snprintf(s_alerted[slot].hex, sizeof(s_alerted[slot].hex), "%s", a->hex);
    s_alerted[slot].at = now;
    if (scene_engine_play("plane", 0)) {
        s_alert_busy_until = now + ((int64_t)scene_seconds("plane") + 5) * 1000000;
        ESP_LOGI(TAG, "Overhead: %s at %.1f nm, playing the plane scene", a->call[0] ? a->call : a->hex, a->dist_nm);
    }
}

/* Publishing has its own (not inlined) functions so the 700-byte values
 * buffer is never on the stack while http_fetch does its TLS handshake. */
static __attribute__((noinline)) void publish_none(const char *line, const char *status, uint32_t ttl_s)
{
    values_t v;
    values_none(&v, line);
    publish(&v, ttl_s);
    value_store_set("plane_status", status, 0);
}

static __attribute__((noinline)) void publish_found(const cfg_t *c, const scan_t *s, const route_t *route)
{
    values_t v;
    values_make(&v, s->have ? &s->near : NULL, s->count, route, c->lat, c->lon, s_radius);
    publish(&v, c->poll_s * 4 + 10);
    value_store_set("plane_status", "OK", 0);
    if (strcmp(s_shown, s->have ? s->near.hex : "")) {
        ESP_LOGI(TAG, "Nearest of %d: %s, %s", s->count, v.v[V_LINE], v.v[V_WHERE]);
        snprintf(s_shown, sizeof(s_shown), "%s", s->have ? s->near.hex : "");
    }
}

/* One poll: fetch, pick, look up the route, publish, maybe alert.
 * Returns the seconds to wait before the next one. */
static int poll_and_publish(const cfg_t *c, int *fails)
{
    scan_t s;
    size_t got = 0;
    int r = fetch_list(c, &s, &got);
    if (r == POLL_BIG && s_radius > 2) {
        s_radius = s_radius * 2 / 3 > 2 ? s_radius * 2 / 3 : 2;
        ESP_LOGW(TAG, "Too many aircraft to read in one go; looking within %d nm for now", s_radius);
        return 2;
    }
    if (r != POLL_OK) {
        int wait = c->poll_s << (*fails < 5 ? *fails + 1 : 6);       // 2x, 4x, ... up to 10 minutes
        if (wait > 600) wait = 600;
        if (++*fails >= 2) publish_none("NO PLANE DATA", "OFFLINE", wait + 30);   // one miss keeps the last values
        return wait;
    }
    *fails = 0;
    if (got < BODY_MAX / 4 && s_radius < c->radius_nm) s_radius++;  // room again: widen step by step
    const route_t *route = s.have ? route_for(&s.near) : NULL;
    publish_found(c, &s, route);
    if (s.have) maybe_alert(c, &s.near);
    return c->poll_s;
}

static void aircraft_task(void *arg)
{
    cfg_t c;
    uint32_t gen = feeds_settings_generation();
    read_cfg(&c);
    s_radius = c.radius_nm;
    int fails = 0;
    bool first = true;
    for (;;) {
        int wait_s = 0;                                               // 0 = until the settings change
        if (!c.on) {
            for (int i = 0; i < V_N; i++) value_store_delete(KEYS[i]);
            value_store_delete("plane_status");
            s_shown[0] = '\0';
        } else if (!c.have_loc) {
            publish_none("NO LOCATION SET", "NO LOCATION", 0);
        } else {
            wait_s = poll_and_publish(&c, &fails);
            if (first) {
                first = false;
                ESP_LOGI(TAG, "First poll done; %u bytes of stack to spare", (unsigned)uxTaskGetStackHighWaterMark(NULL));
            }
        }
        for (int t = 0; wait_s == 0 || t < wait_s; t++) {              // wait, looking for saved settings each second
            vTaskDelay(pdMS_TO_TICKS(1000));
            uint32_t g = feeds_settings_generation();
            if (g == gen) continue;
            gen = g;
            cfg_t n;
            read_cfg(&n);
            if (cfg_same(&n, &c)) continue;                           // someone else's settings (weather, clock)
            c = n;
            s_radius = c.radius_nm;
            fails = 0;
            break;
        }
    }
}

void feed_aircraft_start(void)
{
    if (xTaskCreate(aircraft_task, "aircraft", 8192, NULL, 3, NULL) != pdPASS) ESP_LOGE(TAG, "Could not start the aircraft feed");
}

#endif /* AIRCRAFT_HOST_TEST */
