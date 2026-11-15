/* Live values: template syntax, lookup order and pushed values (live_values.h).
 * Pure C, checked against web/src/lib/values.js by hello_world/host_test. */

#include "live_values.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A number at the start of s the way JavaScript's parseFloat() reads it, so
 * the globe and the preview agree on text like "0x10", "inf" or " 42abc":
 * optional sign, digits, point, exponent. (strtod() alone would also take hex,
 * "inf" and "nan".) False where parseFloat gives NaN; *v can be +-inf on overflow. */
static bool js_number(const char *s, double *v)
{
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    const char *p = s + (*s == '+' || *s == '-');
    const char *d0 = p;
    while (*p >= '0' && *p <= '9') p++;
    bool digits = p > d0;
    if (*p == '.') {
        const char *f = ++p;
        while (*p >= '0' && *p <= '9') p++;
        digits = digits || p > f;
    }
    if (!digits) return false;
    if (*p == 'e' || *p == 'E') {
        const char *e = p + 1 + (p[1] == '+' || p[1] == '-');
        if (*e >= '0' && *e <= '9') { while (*e >= '0' && *e <= '9') e++; p = e; }
    }
    char buf[64];
    size_t n = (size_t)(p - s) < sizeof(buf) ? (size_t)(p - s) : sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
    *v = strtod(buf, NULL);
    return true;
}

/* Pushed values (value_store): {var:cpu}; {bar:cpu} and {pct:cpu} for a 0-100
 * number, {bar:print:250} / {pct:print:250} for one out of 250. The clock's
 * own periods ({bar:day} ...) are left to clock_value(). A name too long to be
 * a key simply isn't set. */
static bool pushed_value(const char *name, const char *arg, const live_env_t *env, char *out, size_t cap)
{
    if (!arg) return false;
    if (!strcmp(name, "var")) {
        if (!env->get || !env->get(arg, out, cap)) snprintf(out, cap, "--");
        return true;
    }
    bool is_bar = !strcmp(name, "bar");
    if (!is_bar && strcmp(name, "pct")) return false;
    static const char *periods[] = { "min", "hour", "day", "week", "month", "year" };
    for (size_t i = 0; i < sizeof(periods) / sizeof(periods[0]); i++) {
        if (!strcmp(arg, periods[i])) return false;
    }
    char key[LIVE_KEY_MAX], text[48];
    const char *colon = strchr(arg, ':');
    size_t klen = colon ? (size_t)(colon - arg) : strlen(arg);
    double max, v = 0;
    if (!colon || !js_number(colon + 1, &max) || !(max > 0)) max = 100;
    bool have = false;
    if (klen < sizeof(key) && env->get) {
        memcpy(key, arg, klen);
        key[klen] = '\0';
        have = env->get(key, text, sizeof(text)) && js_number(text, &v) && isfinite(v);
    }
    double frac = have ? v / max : 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    if (is_bar) {
        int width = cap > 10 ? 10 : (int)cap - 1, full = (int)(frac * width + 0.0001);
        for (int i = 0; i < width; i++) out[i] = i < full ? '#' : '-';
        out[width] = '\0';
    } else if (have) {
        snprintf(out, cap, "%d", (int)(frac * 100 + 0.0001));
    } else {
        snprintf(out, cap, "--");
    }
    return true;
}

bool live_value(const char *name, const char *arg, const live_env_t *env, char *out, size_t cap)
{
    if (!strcmp(name, "ip")) {
        out[0] = '\0';
        if (env->ip) env->ip(out, cap);
        return true;
    }
    if (pushed_value(name, arg, env, out, cap)) return true;   // before clock_value: {bar:cpu} is a pushed bar, {bar:day} a clock one
    if (clock_value(name, arg, env->now, out, cap)) return true;
    // {cpu}: any pushed value by its bare name, except built-ins that only
    // work with an argument ({tz}, {until}, {countdown}, {var} stay as typed, as in the preview)
    if (arg || !strcmp(name, "var") || !strcmp(name, "tz") || !strcmp(name, "until") || !strcmp(name, "countdown")) return false;
    return env->get && env->get(name, out, cap);
}

static bool name_char(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

/* Length of the value token at p, 0 if there is none. The grammar is the
 * preview's regex /\{([a-z0-9_]{1,15})(?::([^{}]{1,24}))?\}/: a name of 1-15
 * of a-z 0-9 _, then optionally ':' and 1-24 characters other than braces. */
static size_t token(const char *p, size_t *name_len, size_t *arg_len)
{
    size_t n = 0, a = 0;
    if (*p != '{') return 0;
    while (n < 16 && name_char(p[1 + n])) n++;
    if (n < 1 || n > 15) return 0;
    *name_len = n;
    *arg_len = 0;
    const char *q = p + 1 + n;
    if (*q == '}') return n + 2;
    if (*q != ':') return 0;
    while (a < 25 && q[1 + a] && q[1 + a] != '{' && q[1 + a] != '}') a++;
    if (a < 1 || a > 24 || q[1 + a] != '}') return 0;
    *arg_len = a;
    return n + a + 3;
}

void live_subst(const char *tmpl, const live_env_t *env, char *out, size_t cap, bool *live)
{
    size_t o = 0;
    bool lv = false;
    if (!cap) return;
    for (const char *p = tmpl; *p && o + 1 < cap;) {
        size_t nlen, alen, len = token(p, &nlen, &alen);
        if (len) {
            char name[16], arg[25], val[48];
            memcpy(name, p + 1, nlen);
            name[nlen] = '\0';
            memcpy(arg, p + 2 + nlen, alen);
            arg[alen] = '\0';
            const char *src = p;                            // unknown: the token stays as typed
            size_t n = len;
            if (live_value(name, alen ? arg : NULL, env, val, sizeof(val))) {
                lv = true;
                src = val;
                n = strlen(val);
            }
            for (size_t i = 0; i < n && o + 1 < cap; i++) out[o++] = src[i];
            p += len;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
    if (live) *live = lv;
}
