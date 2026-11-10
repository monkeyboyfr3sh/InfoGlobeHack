/* Host (PC) build of the globe's live values: main/src/clock_values.c and
 * live_values.c, unchanged. `npm test` in web/ compiles this with gcc and
 * feeds it the cases from web/src/lib/values.vectors.json plus a generated
 * sweep, then compares the output with the preview (values.js); see
 * web/src/lib/values.vectors.test.js. To build it by hand:
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -I ../main/inc live_values_host.c \
 *       ../main/src/clock_values.c ../main/src/live_values.c -o live_values_host -lm
 *
 * Input, one case per line, fields separated by tabs:
 *   id  utc_ms  offset_s  lat  lon  colon_period  colon_look  steady  vars  template
 *     lat, lon      empty = no location set
 *     colon_period  empty = the frame has no "blinkColon"
 *     colon_look    "dot" or "hide"
 *     steady        1 = no blinking (the engine's change detection)
 *     vars          pushed values as key=value, separated by \x1f
 * Output, one line per case: id \t text
 *
 * The local UTC offset comes from the test (Node's tz database), because the
 * globe gets it from newlib's TZ handling, which a PC C library can't stand
 * in for. Everything after that point is the globe's own code. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "live_values.h"

#define MAX_VARS 32

static struct { char key[LIVE_KEY_MAX]; char text[64]; } s_vars[MAX_VARS];
static int s_nvars;

static bool get_var(const char *key, char *out, size_t cap)
{
    for (int i = 0; i < s_nvars; i++) {
        if (!strcmp(s_vars[i].key, key)) { snprintf(out, cap, "%s", s_vars[i].text); return true; }
    }
    return false;
}

static void get_ip(char *out, size_t cap) { snprintf(out, cap, "10.0.0.5"); }

static void parse_vars(char *s)
{
    s_nvars = 0;
    while (s && *s && s_nvars < MAX_VARS) {
        char *next = strchr(s, '\x1f');
        if (next) *next++ = '\0';
        char *eq = strchr(s, '=');
        if (eq) {
            *eq = '\0';
            snprintf(s_vars[s_nvars].key, sizeof(s_vars[0].key), "%s", s);
            snprintf(s_vars[s_nvars].text, sizeof(s_vars[0].text), "%s", eq + 1);
            s_nvars++;
        }
        s = next;
    }
}

int main(void)
{
    static char line[8192], out[4096];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        char *f[10];
        int n = 0;
        for (char *p = line; n < 10;) {
            f[n++] = p;
            char *tab = strchr(p, '\t');
            if (!tab) break;
            *tab = '\0';
            p = tab + 1;
        }
        if (n != 10) { printf("%s\tBAD INPUT LINE\n", f[0]); continue; }

        long long ms = atoll(f[1]);
        long long sec = ms >= 0 ? ms / 1000 : -((-ms + 999) / 1000);
        clock_now_t now = { 0 };
        clock_now_set(&now, sec, (int32_t)((ms - sec * 1000) * 1000), atoi(f[2]));
        clock_values_set_location(f[3][0] ? atof(f[3]) : NAN, f[4][0] ? atof(f[4]) : NAN);
        now.colon_set = f[5][0] != '\0';
        now.colon_period = (uint16_t)atoi(f[5]);
        now.colon_look = strcmp(f[6], "dot") ? ' ' : '.';
        now.steady = f[7][0] == '1';
        parse_vars(f[8]);

        live_env_t env = { .now = &now, .get = get_var, .ip = get_ip };
        live_subst(f[9], &env, out, sizeof(out), NULL);
        printf("%s\t%s\n", f[0], out);
    }
    return 0;
}
