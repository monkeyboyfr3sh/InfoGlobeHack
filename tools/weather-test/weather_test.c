/* PC test for the weather feed's parser (hello_world/main/src/feed_weather.c):
 * real Open-Meteo answers saved next to this file, plus edge cases.
 *
 * cJSON comes from the firmware's managed components (run idf.py build once):
 *   gcc -Wall -Wextra -I hello_world/managed_components/espressif__cjson/cJSON \
 *       tools/weather-test/weather_test.c hello_world/managed_components/espressif__cjson/cJSON/cJSON.c \
 *       -lm -o weather_test && ./weather_test
 */

#define WEATHER_HOST_TEST
#include "../../hello_world/main/src/feed_weather.c"

#include <ctype.h>

static int s_fail;
static char s_dir[512];

#define CHECK(cond, ...) do { if (!(cond)) { s_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *name)
{
    char path[640];
    snprintf(path, sizeof(path), "%s%s", s_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAIL: cannot open %s\n", path); s_fail++; return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    b[fread(b, 1, n, f)] = '\0';
    fclose(f);
    return b;
}

/* Only glyphs the globe's font has, and short enough for a value. */
static void check_glyphs(char v[W_N][WX_TEXT])
{
    for (int i = 0; i < W_N; i++) {
        for (const char *c = v[i]; *c; c++) CHECK(isupper((unsigned char)*c) || isdigit((unsigned char)*c) || *c == ' ' || *c == '-', "%s = '%s' has '%c'", WX_KEYS[i], v[i], *c);
        CHECK(strlen(v[i]) <= 15, "%s = '%s' is long", WX_KEYS[i], v[i]);
    }
}

/* expect: "temp=45 sky=CLEAR ..." (a key with nothing after '=' must be empty) */
static void expect(const char *label, char v[W_N][WX_TEXT], const char *want)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", want);
    for (char *tok = strtok(buf, "|"); tok; tok = strtok(NULL, "|")) {
        char *eq = strchr(tok, '=');
        *eq = '\0';
        int k = -1;
        for (int i = 0; i < W_N; i++) if (!strcmp(WX_KEYS[i], tok)) k = i;
        CHECK(k >= 0, "%s: unknown key %s", label, tok);
        if (k >= 0) CHECK(!strcmp(v[k], eq + 1), "%s: %s = '%s', want '%s'", label, tok, v[k], eq + 1);
    }
}

static void print_values(const char *label, char v[W_N][WX_TEXT])
{
    printf("%-14s", label);
    for (int i = 0; i < W_N; i++) printf(" %s=%s", WX_KEYS[i], v[i]);
    printf("\n");
}

static char s_v[W_N][WX_TEXT];

static void real_answers(void)
{
    char *b = slurp("chicago-f.json");          // fahrenheit + mph, clear night
    if (b) {
        CHECK(wx_parse(b, s_v), "chicago: parse");
        print_values("chicago (F)", s_v);
        expect("chicago", s_v, "temp=45|feels=39|hi=58|lo=36|humid=76|wind=8|winddir=SW|sky=CLEAR|rain=0|thi=59|tlo=40|tsky=CLOUDY|weather=45 CLEAR");
        check_glyphs(s_v);
        free(b);
    }
    b = slurp("bergen-c.json");                 // celsius + km/h, drizzle, rain tomorrow
    if (b) {
        CHECK(wx_parse(b, s_v), "bergen: parse");
        print_values("bergen (C)", s_v);
        expect("bergen", s_v, "temp=7|feels=4|hi=8|lo=6|humid=91|wind=13|winddir=SE|sky=DRIZZLE|rain=90|thi=7|tlo=2|tsky=RAIN|weather=7 DRIZZLE");
        check_glyphs(s_v);
        free(b);
    }
}

static void edge_cases(void)
{
    // daytime, sub-zero rounding, no rain chance (null), no tomorrow, an extra past day first
    const char *j1 = "{\"current\":{\"time\":\"2026-01-05T12:00\",\"temperature_2m\":-0.4,\"apparent_temperature\":-12.6,"
        "\"relative_humidity_2m\":55,\"weather_code\":0,\"wind_speed_10m\":0.2,\"wind_direction_10m\":359,\"is_day\":1},"
        "\"daily\":{\"time\":[\"2026-01-04\",\"2026-01-05\"],\"weather_code\":[71,75],\"temperature_2m_max\":[1,2.5],"
        "\"temperature_2m_min\":[-25,-19.5],\"precipitation_probability_max\":[10,null]}}";
    CHECK(wx_parse(j1, s_v), "j1: parse");
    print_values("edge 1", s_v);
    expect("j1", s_v, "temp=0|feels=-13|winddir=N|sky=SUNNY|weather=0 SUNNY|hi=3|lo=-20|rain=|thi=|tlo=|tsky=|wind=0|humid=55");
    check_glyphs(s_v);

    // unknown weather code, no daily block, no wind
    const char *j2 = "{\"current\":{\"time\":\"2026-01-05T12:00\",\"temperature_2m\":101.5,\"weather_code\":42}}";
    CHECK(wx_parse(j2, s_v), "j2: parse");
    expect("j2", s_v, "temp=102|sky=|weather=102|hi=|lo=|wind=|winddir=|feels=|humid=|rain=");

    // not a forecast
    CHECK(!wx_parse("{\"error\":true,\"reason\":\"Latitude must be in range of -90 to 90\"}", s_v), "error answer accepted");
    CHECK(!wx_parse("<html>502 Bad Gateway</html>", s_v), "html accepted");
    CHECK(!wx_parse("", s_v), "empty accepted");
    CHECK(!wx_parse("{\"current\":{\"temperature_2m\":null}}", s_v), "null temperature accepted");
    for (int i = 0; i < W_N; i++) CHECK(!s_v[i][0], "value %s left over after a failed parse", WX_KEYS[i]);
}

static void compass(void)
{
    struct { double deg; const char *want; } T[] = {
        { 0, "N" }, { 22.4, "N" }, { 22.6, "NE" }, { 90, "E" }, { 140, "SE" }, { 180, "S" }, { 237, "SW" },
        { 270, "W" }, { 337.4, "NW" }, { 337.6, "N" }, { 359, "N" }, { 360, "N" }, { -30, "NW" },
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) CHECK(!strcmp(wx_compass(T[i].deg), T[i].want), "compass %.1f = %s, want %s", T[i].deg, wx_compass(T[i].deg), T[i].want);
}

static void sky_words(void)
{
    static const int CODES[] = { 0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99 };
    for (size_t i = 0; i < sizeof(CODES) / sizeof(CODES[0]); i++) {
        for (int day = 0; day < 2; day++) {
            const char *w = wx_sky(CODES[i], day);
            CHECK(w != NULL, "WMO %d has no word", CODES[i]);
            if (!w) continue;
            CHECK(strlen(w) <= 10, "WMO %d word '%s' is long", CODES[i], w);
            for (const char *c = w; *c; c++) CHECK(isupper((unsigned char)*c) || *c == ' ', "WMO %d word '%s'", CODES[i], w);
        }
    }
}

static void keys_and_url(void)
{
    for (int i = 0; i < W_N; i++) {
        size_t n = strlen(WX_KEYS[i]);
        CHECK(n >= 1 && n <= 15, "key %s length", WX_KEYS[i]);
        for (const char *c = WX_KEYS[i]; *c; c++) CHECK(islower((unsigned char)*c) || isdigit((unsigned char)*c) || *c == '_', "key %s", WX_KEYS[i]);
    }
    // esp_http_client builds "GET <path>?<query> HTTP/1.1\r\n" in a 512-byte buffer
    char url[448];
    wx_url(url, sizeof(url), -89.99999, -179.99999, false);
    size_t line = strlen("GET ") + strlen(url) - strlen("https://api.open-meteo.com") + strlen(" HTTP/1.1\r\n");
    printf("worst-case request line: %u bytes (limit 511)\n%s\n", (unsigned)line, url);
    CHECK(line < 512, "request line %u bytes", (unsigned)line);
    CHECK(strlen(url) + 1 < sizeof(url), "url buffer full");
    wx_url(url, sizeof(url), 41.88, -87.63, true);
    CHECK(strstr(url, "latitude=41.8800&longitude=-87.6300") && !strstr(url, "fahrenheit"), "metric url: %s", url);
}

int main(int argc, char **argv)
{
    if (argc > 1) snprintf(s_dir, sizeof(s_dir), "%s/", argv[1]);
    else {                                           // samples live next to this file
        snprintf(s_dir, sizeof(s_dir), "%s", __FILE__);
        char *slash = strrchr(s_dir, '/'), *bslash = strrchr(s_dir, '\\');
        if (bslash > slash) slash = bslash;
        if (slash) slash[1] = '\0'; else s_dir[0] = '\0';
    }
    real_answers();
    edge_cases();
    compass();
    sky_words();
    keys_and_url();
    if (s_fail) printf("%d FAILED\n", s_fail);
    else printf("all weather parser tests passed\n");
    return s_fail != 0;
}
