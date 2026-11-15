/* Off-device tests for the aircraft feed (main/src/feed_aircraft.c): real
 * answers saved from adsb.lol, adsb.fi and adsbdb (data/, trimmed) ->
 * nearest aircraft -> values, plus the distance/bearing maths and the memory
 * a busy-sky answer needs. Build and run from the repository root (cJSON is
 * fetched by the first idf.py build):
 *
 *   gcc -std=gnu11 -Wall -Wextra -O1 -DAIRCRAFT_HOST_TEST \
 *       -I hello_world/main/inc -I hello_world/managed_components/espressif__cjson/cJSON \
 *       hello_world/host_test/feed_aircraft_test.c \
 *       hello_world/managed_components/espressif__cjson/cJSON/cJSON.c -lm -o aircraft_test
 *   ./aircraft_test                 (or: ./aircraft_test <path to data/>)
 *
 * The samples were taken around SFO (37.62, -122.38) on 2026-11-10 04:56 UTC. */

#include "../main/src/feed_aircraft.c"

#include <time.h>

static int s_fails, s_checks;
#define CHECK(cond, ...) do { s_checks++; if (!(cond)) { s_fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define CHECK_STR(got, want) CHECK(!strcmp((got), (want)), "got \"%s\", want \"%s\"", (got), (want))

static const double HLAT = 37.62, HLON = -122.38;   // the point the samples were asked for
static char s_dir[512];

static char *load(const char *name, size_t *len)
{
    char path[640];
    snprintf(path, sizeof(path), "%s/%s", s_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("Cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { printf("Cannot read %s\n", path); exit(2); }
    b[n] = '\0';
    fclose(f);
    if (len) *len = (size_t)n;
    return b;
}

/* cJSON allocations, counted: what parsing costs at its peak. */
static size_t s_mem_now, s_mem_peak;
static void *count_malloc(size_t n)
{
    size_t *p = malloc(n + 2 * sizeof(size_t));
    if (!p) return NULL;
    p[0] = n;
    s_mem_now += n;
    if (s_mem_now > s_mem_peak) s_mem_peak = s_mem_now;
    return p + 2;
}
static void count_free(void *q)
{
    if (!q) return;
    size_t *p = (size_t *)q - 2;
    s_mem_now -= p[0];
    free(p);
}

/* Every character one the globe's font can draw (docs/protocol.md). */
static bool drawable(const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E || strchr("%&+;@[\\]^_`{|}~", c)) return false;
    }
    return true;
}

/* The aircraft with this hex from a sample, as the feed parses it. */
static ac_t aircraft(const char *file, const char *hex)
{
    char *b = load(file, NULL);
    cJSON *j = cJSON_Parse(b), *o;
    ac_t a = {0}, found = {0};
    cJSON *arr = cJSON_GetObjectItem(j, "ac");
    if (!arr) arr = cJSON_GetObjectItem(j, "aircraft");
    cJSON_ArrayForEach(o, arr) {
        if (ac_parse(o, HLAT, HLON, &a) && !strcmp(a.hex, hex)) found = a;
    }
    cJSON_Delete(j);
    free(b);
    CHECK(found.hex[0], "%s not airborne in %s", hex, file);
    return found;
}

static route_t route(const char *file)
{
    char *b = load(file, NULL);
    route_t r;
    CHECK(route_parse(b, &r), "%s should parse", file);
    free(b);
    return r;
}

static void test_maths(void)
{
    // Our distance and bearing against the ones the providers computed for the same point.
    const char *files[] = { "adsb_lol_sfo.json", "adsb_fi_v3_sfo.json", "adsb_fi_v2_sfo.json" };
    double worst_d = 0, worst_b = 0;
    int n = 0;
    for (int i = 0; i < 3; i++) {
        char *b = load(files[i], NULL);
        cJSON *j = cJSON_Parse(b), *o;
        cJSON *arr = cJSON_GetObjectItem(j, "ac");
        if (!arr) arr = cJSON_GetObjectItem(j, "aircraft");
        cJSON_ArrayForEach(o, arr) {
            double lat = cJSON_GetObjectItem(o, "lat")->valuedouble, lon = cJSON_GetObjectItem(o, "lon")->valuedouble;
            double d = dist_nm(HLAT, HLON, lat, lon), br = bearing_deg(HLAT, HLON, lat, lon);
            double ed = fabs(d - cJSON_GetObjectItem(o, "dst")->valuedouble);
            double eb = angle_diff(br, cJSON_GetObjectItem(o, "dir")->valuedouble);
            if (ed > worst_d) worst_d = ed;
            if (eb > worst_b) worst_b = eb;
            n++;
        }
        cJSON_Delete(j);
        free(b);
    }
    printf("maths: %d aircraft, worst difference to the providers' own figures: %.3f nm, %.2f deg\n", n, worst_d, worst_b);
    CHECK(n == 31, "31 aircraft in the samples, got %d", n);
    CHECK(worst_d <= 0.02, "distance off by %.3f nm", worst_d);
    CHECK(worst_b <= 0.2, "bearing off by %.2f deg", worst_b);
    // A long one: SFO -> ORD is 1846 statute miles = 1604 nm on the ellipsoid; a sphere is within 0.5 %.
    double sfo_ord = dist_nm(37.6190, -122.3750, 41.9786, -87.9048);
    CHECK(fabs(sfo_ord - 1604) < 8, "SFO-ORD %.1f nm", sfo_ord);
    CHECK(fabs(bearing_deg(0, 0, 1, 0) - 0) < 1e-9 && fabs(bearing_deg(0, 0, 0, 1) - 90) < 1e-9 &&
          fabs(bearing_deg(0, 0, -1, 0) - 180) < 1e-9 && fabs(bearing_deg(0, 0, 0, -1) - 270) < 1e-9, "cardinal bearings");
    const struct { double deg; const char *want; } C[] = {
        { 0, "N" }, { 22.4, "N" }, { 22.6, "NE" }, { 90, "E" }, { 157.37, "SE" }, { 180, "S" },
        { 247, "SW" }, { 290, "W" }, { 300, "NW" }, { 337.4, "NW" }, { 337.6, "N" }, { 359.9, "N" }, { 360, "N" },
    };
    for (size_t i = 0; i < sizeof(C) / sizeof(C[0]); i++) CHECK_STR(compass8(C[i].deg), C[i].want);
    CHECK(fabs(angle_diff(350, 10) - 20) < 1e-9 && fabs(angle_diff(10, 350) - 20) < 1e-9 && fabs(angle_diff(157, 350) - 167) < 1e-9, "angle_diff");
}

static void test_text(void)
{
    char b[48];
    const struct { const char *in, *want; } T[] = {
        { "Montr\xc3\xa9" "al", "MONTREAL" },                       // Montréal
        { "San Jos\xc3\xa9 del Cabo", "SAN JOSE DEL CABO" },
        { "\xc5\x81\xc3\xb3" "d\xc5\xba", "LODZ" },                 // Łódź
        { "S\xc3\xa3o Paulo", "SAO PAULO" },                        // São Paulo
        { "\xc4\xb0stanbul", "ISTANBUL" },                          // İstanbul
        { "Z\xc3\xbcrich", "ZURICH" },
        { "Stra\xc3\x9f" "e", "STRASE" },                           // ß has no capital here: one S
        { "\xe6\x9d\xb1\xe4\xba\xac", "" },                         // 東京: nothing drawable
        { "UAL123  ", "UAL123" },
        { "  a_b%c;d@e  ", "A B C D E" },
        { "x\xf0\x9f\x9b\xa9y", "X Y" },                            // an emoji plane
        { NULL, "" },
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        font_text(b, sizeof(b), T[i].in);
        CHECK_STR(b, T[i].want);
    }
    font_text(b, 5, "MONTREAL");
    CHECK_STR(b, "MONT");
}

/* An aircraft `dist` nm away in direction `brg`, flying `track` at `gs` knots and `alt` feet. */
static ac_t plane_at(double dist, double brg, int track, int gs, int alt)
{
    ac_t a = { .alt_ft = alt, .gs_kt = gs, .track = track, .dist_nm = dist, .brg = brg, .vrate = INT_MIN };
    return a;
}

/* "Will I see or hear it soon": the track carried 75 s ahead, judged by height in the sky and straight-line distance. */
static void test_notice(void)
{
    double in_s;
    ac_t jet = plane_at(8, 0, 180, 480, 35000);                 // 8 nm north at cruise, coming straight at us
    CHECK(fabs(closest_ahead(&jet, 75, &in_s)) < 0.01 && fabs(in_s - 60) < 0.5, "jet is overhead in 60 s, got %.2f nm in %.0f s", closest_ahead(&jet, 75, &in_s), in_s);
    CHECK(noticeable(&jet, 2, 75, NULL) && noticeable(&jet, 1, 75, NULL), "a jet about to pass overhead is noticed");
    ac_t far = plane_at(8, 0, 90, 480, 35000);                  // same jet, flying across: never closer than 8 nm
    CHECK(fabs(closest_ahead(&far, 75, &in_s) - 8) < 0.01 && in_s == 0, "crossing jet is closest now");
    CHECK(!noticeable(&far, 2, 75, NULL) && noticeable(&far, 3, 75, NULL), "8 nm off at cruise: a speck, only the widest setting counts it");
    ac_t gone = plane_at(3, 0, 0, 480, 35000);                  // 3 nm north and flying away
    CHECK(noticeable(&gone, 2, 75, NULL), "still high in the sky as it leaves");
    ac_t gone2 = plane_at(6, 0, 0, 480, 35000);
    CHECK(!noticeable(&gone2, 2, 75, NULL), "and gone at 6 nm");
    ac_t late = plane_at(14, 0, 180, 480, 35000);               // heading over us, but 105 s out
    CHECK(!noticeable(&late, 2, 75, NULL), "too far ahead to call it soon");
    ac_t cessna = plane_at(3, 90, 270, 100, 1500);              // low and slow, 3 nm east, coming this way
    CHECK(noticeable(&cessna, 2, 75, &in_s) && in_s > 70, "a low plane that will pass within earshot (in %.0f s)", in_s);
    ac_t cessna_by = plane_at(3, 90, 0, 100, 1500);             // same, flying past
    CHECK(!noticeable(&cessna_by, 2, 75, NULL) && noticeable(&cessna_by, 3, 75, NULL), "3 nm off and low: only the widest setting");
    ac_t parked = plane_at(2.5, 45, -1, 0, 200);                // at the airfield down the road, still reporting a height
    CHECK(!noticeable(&parked, 2, 75, NULL), "a plane sitting at an airfield 2.5 nm away is neither seen nor heard");
    ac_t heli = plane_at(0.8, 200, -1, -1, 800);                // no track or speed known: where it is now
    CHECK(noticeable(&heli, 2, 75, NULL) && noticeable(&heli, 1, 75, NULL), "close by, track unknown");
    ac_t arrival = plane_at(2.3, 300, 30, 180, 10000);          // traffic at 10,000 ft flying past, never nearer than 2.3 nm
    CHECK(noticeable(&arrival, 2, 75, NULL) && !noticeable(&arrival, 1, 75, NULL), "high enough in the sky to see, not overhead");
    CHECK(!noticeable(&jet, 0, 75, NULL) && !noticeable(&jet, 9, 75, NULL), "never, and nonsense levels");
}

static bool want_not_nearest(const ac_t *a, void *ctx) { (void)ctx; return strcmp(a->hex, "ab819c") != 0; }
static bool want_none(const ac_t *a, void *ctx) { (void)a; (void)ctx; return false; }

static void test_scan(void)
{
    scan_t s;
    size_t len;
    char *b = load("adsb_lol_sfo.json", &len);
    CHECK(ac_scan(b, len, HLAT, HLON, 10, NULL, &s), "lol sample parses");
    CHECK(s.count == 6 && s.have, "6 airborne (5 on the ground, a vehicle among them), got %d", s.count);
    CHECK_STR(s.near.hex, "ab819c");
    CHECK_STR(s.near.call, "UAL1367");
    CHECK_STR(s.near.reg, "N840UA");
    CHECK_STR(s.near.type, "A319");
    CHECK(s.near.alt_ft == 1575 && s.near.gs_kt == 136 && s.near.track == 6 && s.near.vrate == 2304, "fields %d %d %d %d",
          s.near.alt_ft, s.near.gs_kt, s.near.track, s.near.vrate);
    CHECK(fabs(s.near.dist_nm - 1.783) < 0.002, "dist %.3f", s.near.dist_nm);
    CHECK(ac_scan(b, len, HLAT, HLON, 5, NULL, &s) && s.count == 4, "within 5 nm: 4, got %d", s.count);
    CHECK(ac_scan(b, len, HLAT, HLON, 1, NULL, &s) && s.count == 0 && !s.have, "within 1 nm: none");
    // Sticky: UAL1970 at 2.25 nm stays shown against UAL1367 at 1.78; ASA1303 at 4.0 does not.
    CHECK(ac_scan(b, len, HLAT, HLON, 10, "a2b12d", &s) && !strcmp(s.near.hex, "a2b12d"), "sticky keeps a2b12d, got %s", s.near.hex);
    CHECK(ac_scan(b, len, HLAT, HLON, 10, "ab761c", &s) && !strcmp(s.near.hex, "ab819c"), "sticky lets go of ab761c, got %s", s.near.hex);
    // The alert pick: the nearest aircraft the caller wants, which need not be the nearest of all.
    CHECK(ac_scan_for(b, len, HLAT, HLON, 10, NULL, STICKY_NM, want_not_nearest, NULL, &s) && s.have_alert &&
          !strcmp(s.near.hex, "ab819c") && !strcmp(s.alert.hex, "a2b12d"), "alert pick skips the nearest, got %s", s.alert.hex);
    CHECK(ac_scan_for(b, len, HLAT, HLON, 10, NULL, STICKY_NM, want_none, NULL, &s) && !s.have_alert, "nothing wanted: no alert pick");
    CHECK(ac_scan_for(b, len, HLAT, HLON, 10, "ab761c", 1e9, NULL, NULL, &s) && !strcmp(s.near.hex, "ab761c"), "held: ab761c stays shown at any distance, got %s", s.near.hex);
    // A cut-off answer is not a list.
    CHECK(!ac_scan(b, len / 2, HLAT, HLON, 10, NULL, &s), "half an answer is rejected");
    free(b);

    b = load("adsb_fi_v3_sfo.json", &len);
    CHECK(ac_scan(b, len, HLAT, HLON, 10, NULL, &s) && s.count == 8, "fi v3: 8 airborne, got %d", s.count);
    CHECK_STR(s.near.call, "UAL1970");
    CHECK(s.near.alt_ft == -100, "landing below sea-level pressure altitude: %d", s.near.alt_ft);
    free(b);

    b = load("adsb_fi_v2_sfo.json", &len);                   // "aircraft" array after "now"
    CHECK(ac_scan(b, len, HLAT, HLON, 10, NULL, &s) && s.count == 6, "fi v2: 6 airborne, got %d", s.count);
    CHECK_STR(s.near.call, "UAL1367");
    free(b);

    const struct { const char *body; bool ok; int count; const char *call; } E[] = {
        { "{\"ac\":[],\"msg\":\"No error\",\"now\":1794286592000}", true, 0, NULL },
        { "{ \"ac\" : [ ] }", true, 0, NULL },
        { "{\"error\": \"Please contact us at contact@airplanes.live.\"}", false, 0, NULL },   // a real 403 body
        { "<html>502 Bad Gateway</html>", false, 0, NULL },
        { "{\"msg\":\"the word \\\"ac\\\" in text\",\"ac\":[{\"hex\":\"abc123\",\"alt_baro\":3000,\"lat\":37.63,\"lon\":-122.38}]}", true, 1, "" },
        { "{\"ac\":[{\"hex\":\"abc123\",\"flight\":\"A}B{C\\\"]\",\"alt_baro\":5000,\"lat\":37.63,\"lon\":-122.38}]}", true, 1, "A B C\"" },
        { "{\"ac\":[{\"hex\":\"abc123\",\"alt_geom\":5000,\"lat\":37.63,\"lon\":-122.38}]}", true, 1, "" },      // geometric altitude only
        { "{\"ac\":[{\"hex\":\"abc123\",\"lat\":37.63,\"lon\":-122.38}]}", true, 0, NULL },                    // no altitude at all
        { "{\"ac\":[{\"hex\":\"abc123\",\"alt_baro\":5000,\"lastPosition\":{\"lat\":37.63,\"lon\":-122.38}}]}", true, 0, NULL },
        { "{\"ac\":[{\"hex\":\"abc123\",\"alt_baro\":5000,\"seen_pos\":75.0,\"lat\":37.63,\"lon\":-122.38}]}", true, 0, NULL },
        { "{\"ac\":[{\"hex\":\"abc123\",\"alt_baro\":5000,\"category\":\"C3\",\"lat\":37.63,\"lon\":-122.38}]}", true, 0, NULL },
        { "{\"ac\":[{\"hex\":\"abc123\",\"alt_baro\":5000,\"lat\":37.63,\"lon\":-122.38}", false, 0, NULL },    // cut off
    };
    for (size_t i = 0; i < sizeof(E) / sizeof(E[0]); i++) {
        bool ok = ac_scan(E[i].body, strlen(E[i].body), HLAT, HLON, 10, NULL, &s);
        CHECK(ok == E[i].ok && (!ok || s.count == E[i].count), "case %u: ok %d count %d", (unsigned)i, ok, s.count);
        if (ok && E[i].call) CHECK_STR(s.near.call, E[i].call);
    }
}

static void test_routes(void)
{
    route_t r = route("adsbdb_ASA1386.json");
    CHECK(r.n == 2, "two airports");
    CHECK_STR(r.ap[0].code, "PVR");
    CHECK_STR(r.ap[0].city, "PUERTO VALLARTA");
    CHECK_STR(r.ap[1].code, "SFO");
    CHECK_STR(r.ap[1].city, "SAN FRANCISCO");
    route_t sjd = route("adsbdb_ASA1303.json");
    CHECK_STR(sjd.ap[0].city, "SAN JOSE DEL CABO");
    char *b = load("adsbdb_unknown.json", NULL);                 // {"response":"unknown callsign"} (HTTP 404)
    CHECK(!route_parse(b, &r), "unknown callsign has no route");
    free(b);
    CHECK(!route_parse("not json", &r) && !route_parse("{\"response\":{\"flightroute\":{\"origin\":{}}}}", &r), "junk has no route");
    const char *no_iata = "{\"response\":{\"flightroute\":{\"callsign\":\"XYZ1\",\"origin\":{\"iata_code\":\"\",\"icao_code\":\"KSFO\","
                          "\"latitude\":37.6,\"longitude\":-122.4,\"municipality\":\"San Francisco\"},\"destination\":{\"iata_code\":null,"
                          "\"icao_code\":\"KORD\",\"latitude\":41.98,\"longitude\":-87.9,\"municipality\":\"Chicago\"}}}}";
    CHECK(route_parse(no_iata, &r) && !strcmp(r.ap[0].code, "KSFO") && !strcmp(r.ap[1].code, "KORD"), "ICAO codes when there is no IATA code");

    // The real routes against the real aircraft: adsbdb was right about 1 of these 5.
    ac_t asa1386 = aircraft("adsb_fi_v3_sfo.json", "aaf47a");
    route_t pvr_sfo = route("adsbdb_ASA1386.json");
    CHECK(route_leg(&pvr_sfo, &asa1386) == 0, "ASA1386 PVR-SFO, 8.7 nm out at 10800 ft: plausible");
    ac_t asa1359 = aircraft("adsb_lol_sfo.json", "a87428");
    route_t sjd_sfo = route("adsbdb_ASA1359.json");
    CHECK(route_leg(&sjd_sfo, &asa1359) < 0, "ASA1359 'SJD-SFO' climbing away from SFO: rejected");
    ac_t ual1970 = aircraft("adsb_lol_sfo.json", "a2b12d"), ual1970b = aircraft("adsb_fi_v3_sfo.json", "a2b12d");
    route_t sfo_las = route("adsbdb_UAL1970.json");
    CHECK(route_leg(&sfo_las, &ual1970) < 0 && route_leg(&sfo_las, &ual1970b) < 0, "UAL1970 'SFO-LAS' landing at SFO: rejected");
    ac_t asa1303 = aircraft("adsb_lol_sfo.json", "ab761c");
    CHECK(route_leg(&sjd, &asa1303) < 0, "ASA1303 'SJD-PDX' flying SSE: rejected");
    ac_t ual1367 = aircraft("adsb_lol_sfo.json", "ab819c");
    route_t bos_iah = route("adsbdb_UAL1367.json");
    CHECK(route_leg(&bos_iah, &ual1367) < 0, "UAL1367 'BOS-IAH' over SFO: rejected");

    // En route halfway PVR -> SFO: plausible heading for SFO, not heading away.
    ac_t mid = asa1386;
    mid.lat = 29.5; mid.lon = -114.5; mid.alt_ft = 37000; mid.vrate = 0;
    mid.track = (int)bearing_deg(mid.lat, mid.lon, 37.619, -122.375);
    CHECK(route_leg(&pvr_sfo, &mid) == 0, "en route, heading for SFO");
    mid.track = (mid.track + 180) % 360;
    CHECK(route_leg(&pvr_sfo, &mid) < 0, "en route, heading away from SFO");
    mid.track = -1;
    CHECK(route_leg(&pvr_sfo, &mid) == 0, "en route, heading unknown");
    // One-stop route: the leg the aircraft is on.
    const char *stop = "{\"response\":{\"flightroute\":{\"callsign\":\"SWA1\",\"origin\":{\"iata_code\":\"SFO\",\"latitude\":37.62,\"longitude\":-122.38,"
                       "\"municipality\":\"San Francisco\"},\"midpoint\":{\"iata_code\":\"LAS\",\"latitude\":36.08,\"longitude\":-115.15,"
                       "\"municipality\":\"Las Vegas\"},\"destination\":{\"iata_code\":\"DEN\",\"latitude\":39.86,\"longitude\":-104.67,\"municipality\":\"Denver\"}}}}";
    route_t three;
    CHECK(route_parse(stop, &three) && three.n == 3 && !strcmp(three.ap[1].code, "LAS"), "midpoint");
    mid.lat = 38.0; mid.lon = -110.0; mid.track = 60;
    CHECK(route_leg(&three, &mid) == 1, "between LAS and DEN: second leg");

    ac_t n66vg = aircraft("adsb_fi_v3_sfo.json", "a8b3c2");
    CHECK(airline_callsign(&ual1367) && !airline_callsign(&n66vg), "UAL1367 is a flight, N66VG a registration");
    ac_t t = ual1367;
    const struct { const char *call; bool want; } A[] = { { "RCH123", true }, { "BAW9KN", true }, { "GABCD", false }, { "", false }, { "UA1", false } };
    for (size_t i = 0; i < sizeof(A) / sizeof(A[0]); i++) {
        snprintf(t.call, sizeof(t.call), "%s", A[i].call);
        CHECK(airline_callsign(&t) == A[i].want, "airline_callsign(%s)", A[i].call);
    }
}

/* value_store's rule for keys: 1-15 of a-z 0-9 _ */
static bool key_ok(const char *k)
{
    size_t n = strlen(k);
    return n >= 1 && n < VALUE_KEY_MAX && strspn(k, "abcdefghijklmnopqrstuvwxyz0123456789_") == n;
}

static void check_values(const values_t *v, const char *what)
{
    for (int i = 0; i < V_N; i++) {
        CHECK(key_ok(KEYS[i]), "key %s", KEYS[i]);
        CHECK(strlen(v->v[i]) <= FRAME_CHARS && v->v[i][0], "%s: %s = \"%s\" fits a frame", what, KEYS[i], v->v[i]);
        CHECK(drawable(v->v[i]), "%s: %s = \"%s\" uses glyphs the globe lacks", what, KEYS[i], v->v[i]);
    }
}

static void test_values(void)
{
    values_t v;
    ac_t ual1367 = aircraft("adsb_lol_sfo.json", "ab819c");
    values_make(&v, &ual1367, 6, NULL, HLAT, HLON, 10);
    const char *want[V_N] = { "UAL1367", "A319", "1600", "136", "1.8", "NE", "N", "--", "--", "--", "HEADING N", "6",
                              "UAL1367 A319", "LOOK NE 1.8 NM 1600 FT" };
    for (int i = 0; i < V_N; i++) CHECK_STR(v.v[i], want[i]);
    check_values(&v, "UAL1367");
    // A route that doesn't fit the aircraft is not shown.
    route_t bos_iah = route("adsbdb_UAL1367.json");
    values_make(&v, &ual1367, 6, &bos_iah, HLAT, HLON, 10);
    CHECK_STR(v.v[V_ROUTE], "--");
    CHECK_STR(v.v[V_LINE], "UAL1367 A319");

    ac_t asa1386 = aircraft("adsb_fi_v3_sfo.json", "aaf47a");
    route_t pvr_sfo = route("adsbdb_ASA1386.json");
    values_make(&v, &asa1386, 8, &pvr_sfo, HLAT, HLON, 10);
    CHECK_STR(v.v[V_LINE], "ASA1386 PVR-SFO B38M");
    CHECK_STR(v.v[V_ROUTE], "PVR-SFO");
    CHECK_STR(v.v[V_FROM], "PVR");
    CHECK_STR(v.v[V_TO], "SFO");
    CHECK_STR(v.v[V_TRIP], "FROM PUERTO VALLARTA");     // landing near the globe: where it came from
    CHECK_STR(v.v[V_WHERE], "LOOK NW 8.7 NM 10800 FT");
    CHECK_STR(v.v[V_HDG], "SE");
    check_values(&v, "ASA1386");
    values_make(&v, &asa1386, 8, &pvr_sfo, 38.5, -123.3, 10);    // a globe 70 nm north of SFO
    CHECK_STR(v.v[V_TRIP], "TO SAN FRANCISCO");

    values_make(&v, NULL, 0, NULL, HLAT, HLON, 10);
    const char *none[V_N] = { "--", "--", "--", "--", "--", "--", "--", "--", "--", "--", "QUIET SKIES", "0", "NO PLANES", "NONE WITHIN 10 NM" };
    for (int i = 0; i < V_N; i++) CHECK_STR(v.v[i], none[i]);
    check_values(&v, "none");

    ac_t a = ual1367;                                        // straight overhead, no callsign
    a.dist_nm = 0.3; a.call[0] = '\0';
    values_make(&v, &a, 1, NULL, HLAT, HLON, 10);
    CHECK_STR(v.v[V_PLANE], "N840UA");
    CHECK_STR(v.v[V_WHERE], "OVERHEAD 1600 FT");
    a.reg[0] = '\0'; a.type[0] = '\0'; a.track = -1; a.gs_kt = -1; a.alt_ft = -120;
    snprintf(a.hex, sizeof(a.hex), "~a1b2c3");
    values_make(&v, &a, 1, NULL, HLAT, HLON, 10);
    CHECK_STR(v.v[V_PLANE], "A1B2C3");
    CHECK_STR(v.v[V_LINE], "A1B2C3");
    CHECK_STR(v.v[V_ALT], "-100");
    CHECK_STR(v.v[V_WHERE], "OVERHEAD 0 FT");
    CHECK_STR(v.v[V_TRIP], "--");
    CHECK_STR(v.v[V_SPD], "--");
    check_values(&v, "bare");

    // Worst case: the longest of everything.
    a = ual1367;
    snprintf(a.call, sizeof(a.call), "ABCD1234");
    snprintf(a.type, sizeof(a.type), "B77W");
    a.alt_ft = 99999; a.gs_kt = 1999; a.dist_nm = 25.94; a.brg = 315;
    char body[512];
    snprintf(body, sizeof(body), "{\"response\":{\"flightroute\":{\"origin\":{\"icao_code\":\"KSFO\",\"latitude\":%f,\"longitude\":%f,"
             "\"municipality\":\"Licenciado Gustavo D\xc3\xad" "az Ordaz\"},\"destination\":{\"icao_code\":\"KORD\",\"latitude\":%f,"
             "\"longitude\":%f,\"municipality\":\"Santa Cruz de la Sierra Viru Viru\"}}}}", a.lat - 3, a.lon - 3, a.lat + 3, a.lon + 3);
    route_t w;
    CHECK(route_parse(body, &w), "long route parses");
    CHECK_STR(w.ap[0].city, "LICENCIADO GUSTAVO");                // cut at a word, at most 20
    CHECK_STR(w.ap[1].city, "SANTA CRUZ DE LA");
    a.track = (int)bearing_deg(a.lat, a.lon, w.ap[1].lat, w.ap[1].lon);
    values_make(&v, &a, 999, &w, HLAT, HLON, 25);
    CHECK_STR(v.v[V_LINE], "ABCD1234 KSFO-KORD B77W");
    CHECK_STR(v.v[V_TRIP], "TO SANTA CRUZ DE LA");
    printf("worst case: \"%s\" (%u) / \"%s\" (%u) / \"%s\" (%u)\n", v.v[V_LINE], (unsigned)strlen(v.v[V_LINE]),
           v.v[V_TRIP], (unsigned)strlen(v.v[V_TRIP]), v.v[V_WHERE], (unsigned)strlen(v.v[V_WHERE]));
    check_values(&v, "worst");
}

static void test_memory(void)
{
    // A busy sky: the SFO sample's aircraft repeated to ~120 KB (BODY_MAX is 128 KB).
    size_t len;
    char *one = load("adsb_lol_sfo.json", &len);
    const char *first = strstr(one, "{\"hex\""), *last = strrchr(one, ']');
    size_t chunk = (size_t)(last - first), cap = 130 * 1024, n = 0;
    char *big = malloc(cap);
    int reps = 0;
    n += (size_t)snprintf(big, cap, "{\"ac\":[\n");
    while (n + chunk + 64 < 120 * 1024) {
        if (reps++) big[n++] = ',';
        memcpy(big + n, first, chunk);
        n += chunk;
        while (n && (big[n - 1] == '\n' || big[n - 1] == '\r')) n--;
    }
    n += (size_t)snprintf(big + n, cap - n, "\n],\"msg\":\"No error\"}");
    free(one);

    cJSON_Hooks hooks = { count_malloc, count_free };
    cJSON_InitHooks(&hooks);
    scan_t s;
    s_mem_now = s_mem_peak = 0;
    clock_t t0 = clock();
    bool ok = ac_scan(big, n, HLAT, HLON, 10, NULL, &s);
    double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    size_t scan_peak = s_mem_peak;
    s_mem_now = s_mem_peak = 0;
    cJSON *tree = cJSON_ParseWithLength(big, n);
    size_t tree_peak = s_mem_peak;
    int total = cJSON_GetArraySize(cJSON_GetObjectItem(tree, "ac"));
    cJSON_Delete(tree);
    cJSON_InitHooks(NULL);
    printf("memory: %u KB answer, %d aircraft: one at a time peaks at %u bytes of cJSON; a full tree would take %u KB (%.1fx the text); %.1f ms on this PC\n",
           (unsigned)(n / 1024), total, (unsigned)scan_peak, (unsigned)(tree_peak / 1024), (double)tree_peak / (double)n, ms);
    CHECK(ok && s.count == 6 * reps, "all %d airborne counted, got %d", 6 * reps, s.count);
    CHECK(scan_peak < 8 * 1024, "parsing one aircraft at a time stays small: %u bytes", (unsigned)scan_peak);
    free(big);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        snprintf(s_dir, sizeof(s_dir), "%s", argv[1]);
    } else {                                                  // data/ next to this file
        snprintf(s_dir, sizeof(s_dir), "%s", __FILE__);
        char *slash = strrchr(s_dir, '/'), *bslash = strrchr(s_dir, '\\');
        if (bslash > slash) slash = bslash;
        if (slash) snprintf(slash, sizeof(s_dir) - (size_t)(slash - s_dir), "/data");
        else snprintf(s_dir, sizeof(s_dir), "data");
    }
    test_maths();
    test_text();
    test_scan();
    test_notice();
    test_routes();
    test_values();
    test_memory();
    printf("%d of %d checks passed\n", s_checks - s_fails, s_checks);
    return s_fails ? 1 : 0;
}
