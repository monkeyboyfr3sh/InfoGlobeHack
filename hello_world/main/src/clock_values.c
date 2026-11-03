/* Clock-flavoured live values for scene text: {fuzzy}, {roman}, {moon},
 * {sunset}, {tz:tyo}, {until:12-25}, {bar:day} ... (Clock studio).
 * Mirrored in web/src/lib/values.js so the preview matches the globe.
 * Everything is computed on the globe; only {sunrise}/{sunset} need the
 * location from settings.json. */

#include "clock_values.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double s_lat = NAN, s_lon = NAN;

void clock_values_set_location(double lat, double lon) { s_lat = lat; s_lon = lon; }
bool clock_values_get_location(double *lat, double *lon)
{
    if (isnan(s_lat)) return false;
    *lat = s_lat; *lon = s_lon;
    return true;
}

static const char *ONES[] = { "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN",
    "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN", "SEVENTEEN", "EIGHTEEN", "NINETEEN" };
static const char *TENS[] = { "", "", "TWENTY", "THIRTY", "FORTY", "FIFTY" };

static void number_words(int n, char *out, size_t cap)
{
    if (n < 20) snprintf(out, cap, "%s", ONES[n]);
    else if (n % 10 == 0) snprintf(out, cap, "%s", TENS[n / 10]);
    else snprintf(out, cap, "%s %s", TENS[n / 10], ONES[n % 10]);
}

static int h12(int h) { return h % 12 ? h % 12 : 12; }

static void roman(int n, char *out, size_t cap)
{
    static const int v[] = { 50, 40, 10, 9, 5, 4, 1 };
    static const char *s[] = { "L", "XL", "X", "IX", "V", "IV", "I" };
    size_t o = 0;
    out[0] = '\0';
    if (n == 0) { snprintf(out, cap, "N"); return; }      // nulla
    for (int i = 0; i < 7; i++) {
        while (n >= v[i] && o + 3 < cap) { o += snprintf(out + o, cap - o, "%s", s[i]); n -= v[i]; }
    }
}

static void bar(double frac, int width, char *out, size_t cap)
{
    if (width > (int)cap - 1) width = (int)cap - 1;
    int full = (int)(frac * width + 0.0001);
    for (int i = 0; i < width; i++) out[i] = i < full ? '#' : '-';
    out[width] = '\0';
}

/* Fractions of the current minute/hour/day/week/month/year. */
static double fraction(const char *what, const struct tm *t, double sec)
{
    double day = (t->tm_hour * 3600 + t->tm_min * 60 + sec) / 86400.0;
    if (!strcmp(what, "min"))  return sec / 60.0;
    if (!strcmp(what, "hour")) return (t->tm_min * 60 + sec) / 3600.0;
    if (!strcmp(what, "day"))  return day;
    if (!strcmp(what, "week")) return (((t->tm_wday + 6) % 7) + day) / 7.0;
    if (!strcmp(what, "month")) {
        static const int dm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        int y = t->tm_year + 1900, days = dm[t->tm_mon] + (t->tm_mon == 1 && ((y % 4 == 0 && y % 100) || y % 400 == 0));
        return (t->tm_mday - 1 + day) / days;
    }
    if (!strcmp(what, "year")) {
        int y = t->tm_year + 1900, days = ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 366 : 365;
        return (t->tm_yday + day) / days;
    }
    return 0;
}

/* Moon age in days since the last new moon (mean synodic month). */
static double moon_age(time_t now)
{
    double jd = now / 86400.0 + 2440587.5;
    double age = fmod(jd - 2451550.1, 29.530588853);
    return age < 0 ? age + 29.530588853 : age;
}

static const char *moon_name(double age)
{
    static const char *names[] = { "NEW MOON", "WAXING CRESCENT", "FIRST QUARTER", "WAXING GIBBOUS",
                                   "FULL MOON", "WANING GIBBOUS", "LAST QUARTER", "WANING CRESCENT" };
    int i = (int)floor(age / 29.530588853 * 8 + 0.5) % 8;
    return names[i];
}

/* Local offset from UTC in hours (DST included). */
static double utc_offset_hours(time_t now)
{
    struct tm g, l;
    gmtime_r(&now, &g);
    localtime_r(&now, &l);
    int d = (l.tm_yday - g.tm_yday);
    if (d > 1) d = -1; else if (d < -1) d = 1;          // year wrap
    return d * 24 + (l.tm_hour - g.tm_hour) + (l.tm_min - g.tm_min) / 60.0;
}

/* Sunrise/sunset (NOAA almanac algorithm), local hours or NAN (polar day/night). */
static double sun_event(const struct tm *t, time_t now, bool rise)
{
    if (isnan(s_lat)) return NAN;
    const double D = M_PI / 180;
    int N = t->tm_yday + 1;
    double lngHour = s_lon / 15;
    double tt = N + ((rise ? 6 : 18) - lngHour) / 24;
    double M = 0.9856 * tt - 3.289;
    double L = fmod(M + 1.916 * sin(M * D) + 0.020 * sin(2 * M * D) + 282.634 + 720, 360);
    double RA = fmod(atan(0.91764 * tan(L * D)) / D + 720, 360);
    RA += floor(L / 90) * 90 - floor(RA / 90) * 90;
    RA /= 15;
    double sinDec = 0.39782 * sin(L * D), cosDec = cos(asin(sinDec));
    double cosH = (cos(90.833 * D) - sinDec * sin(s_lat * D)) / (cosDec * cos(s_lat * D));
    if (cosH > 1 || cosH < -1) return NAN;
    double H = (rise ? 360 - acos(cosH) / D : acos(cosH) / D) / 15;
    double T = H + RA - 0.06571 * tt - 6.622;
    double UT = fmod(T - lngHour + 48, 24);
    return fmod(UT + utc_offset_hours(now) + 48, 24);
}

static void hm12(double hours, char *out, size_t cap)
{
    if (isnan(hours)) { snprintf(out, cap, "--:--"); return; }
    int m = (int)floor(hours * 60 + 0.5) % 1440;
    snprintf(out, cap, "%d:%02d", h12(m / 60), m % 60);
}

/* World clock cities -> POSIX TZ (the web app uses the matching IANA names). */
static const struct { const char *key, *tz; } CITIES[] = {
    { "nyc", "EST5EDT,M3.2.0,M11.1.0" }, { "chi", "CST6CDT,M3.2.0,M11.1.0" }, { "den", "MST7MDT,M3.2.0,M11.1.0" },
    { "la", "PST8PDT,M3.2.0,M11.1.0" },  { "hnl", "HST10" },               { "lon", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "par", "CET-1CEST,M3.5.0,M10.5.0/3" }, { "ber", "CET-1CEST,M3.5.0,M10.5.0/3" }, { "ath", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "dxb", "<+04>-4" }, { "del", "IST-5:30" }, { "sgp", "<+08>-8" }, { "hkg", "HKT-8" }, { "tyo", "JST-9" },
    { "syd", "AEST-10AEDT,M10.1.0,M4.1.0/3" }, { "akl", "NZST-12NZDT,M9.5.0,M4.1.0/3" }, { "utc", "UTC0" },
};

static bool city_time(const char *key, time_t now, char *out, size_t cap)
{
    for (size_t i = 0; i < sizeof(CITIES) / sizeof(CITIES[0]); i++) {
        if (strcmp(key, CITIES[i].key)) continue;
        // Swap TZ briefly; only the display task formats local time.
        const char *old = getenv("TZ");
        char saved[64] = "";
        if (old) snprintf(saved, sizeof(saved), "%s", old);
        setenv("TZ", CITIES[i].tz, 1);
        tzset();
        struct tm t;
        localtime_r(&now, &t);
        if (saved[0]) setenv("TZ", saved, 1); else unsetenv("TZ");
        tzset();
        snprintf(out, cap, "%d:%02d", h12(t.tm_hour), t.tm_min);
        return true;
    }
    return false;
}

/* {until:HH:MM} -> "h:mm" to the next such time; {until:MM-DD} -> days to the next such date. */
static bool until_value(const char *arg, const struct tm *t, char *out, size_t cap)
{
    int a, b;
    if (sscanf(arg, "%d:%d", &a, &b) == 2) {
        int now_m = t->tm_hour * 60 + t->tm_min, tgt = a * 60 + b;
        int d = (tgt - now_m + 1440) % 1440;
        snprintf(out, cap, "%d:%02d", d / 60, d % 60);
        return true;
    }
    if (sscanf(arg, "%d-%d", &a, &b) == 2) {
        struct tm x = *t;
        x.tm_mon = a - 1; x.tm_mday = b; x.tm_hour = 12; x.tm_min = x.tm_sec = 0; x.tm_isdst = -1;
        struct tm today = *t;
        today.tm_hour = 12; today.tm_min = today.tm_sec = 0; today.tm_isdst = -1;
        time_t tt = mktime(&x), t0 = mktime(&today);
        if (tt < t0) { x.tm_year++; tt = mktime(&x); }
        snprintf(out, cap, "%ld", (long)((tt - t0 + 43200) / 86400));
        return true;
    }
    return false;
}

bool clock_value(const char *name, const char *arg, const struct timeval *tv, char *out, size_t cap)
{
    time_t now = tv->tv_sec;
    struct tm t;
    localtime_r(&now, &t);
    double sec = t.tm_sec + tv->tv_usec / 1e6;
    char a[24], b[24];

    if (!strcmp(name, "words")) {                       // "TEN TWENTY EIGHT", "TEN O'CLOCK", "TEN OH FIVE"
        number_words(h12(t.tm_hour), a, sizeof(a));
        if (t.tm_min == 0) snprintf(out, cap, "%s O'CLOCK", a);
        else if (t.tm_min < 10) { number_words(t.tm_min, b, sizeof(b)); snprintf(out, cap, "%s OH %s", a, b); }
        else { number_words(t.tm_min, b, sizeof(b)); snprintf(out, cap, "%s %s", a, b); }
        return true;
    }
    if (!strcmp(name, "fuzzy")) {                       // to the nearest five minutes
        int m5 = ((t.tm_min * 60 + t.tm_sec + 150) / 300) * 5;
        int h = t.tm_hour + (m5 > 30 ? 1 : 0);
        if (m5 == 60) { m5 = 0; h = t.tm_hour + 1; }
        h %= 24;
        if (m5 == 0 && (h == 0 || h == 12)) { snprintf(out, cap, "%s", h ? "NOON" : "MIDNIGHT"); return true; }
        number_words(h12(h), a, sizeof(a));
        static const char *rel[] = { "", "FIVE PAST", "TEN PAST", "QUARTER PAST", "TWENTY PAST", "TWENTY FIVE PAST", "HALF PAST",
                                     "TWENTY FIVE TO", "TWENTY TO", "QUARTER TO", "TEN TO", "FIVE TO" };
        if (m5 == 0) snprintf(out, cap, "%s O'CLOCK", a);
        else snprintf(out, cap, "%s %s", rel[m5 / 5], a);
        return true;
    }
    if (!strcmp(name, "roman")) {
        roman(h12(t.tm_hour), a, sizeof(a));
        roman(t.tm_min, b, sizeof(b));
        snprintf(out, cap, "%s:%s", a, b);
        return true;
    }
    if (!strcmp(name, "binary")) {
        for (int i = 0; i < 5; i++) a[i] = (t.tm_hour >> (4 - i)) & 1 ? '1' : '0';
        a[5] = '\0';
        for (int i = 0; i < 6; i++) b[i] = (t.tm_min >> (5 - i)) & 1 ? '1' : '0';
        b[6] = '\0';
        snprintf(out, cap, "%s:%s", a, b);
        return true;
    }
    if (!strcmp(name, "beat")) {                         // Swatch internet time, Biel (UTC+1)
        long s = (long)((now + 3600) % 86400);
        snprintf(out, cap, "%03ld", s * 10 / 864);
        return true;
    }
    if (!strcmp(name, "decimal")) {                      // 10-hour day, 100-minute hour
        long long d = (long long)(t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec) * 100000LL / 86400;
        snprintf(out, cap, "%lld:%02lld", d / 10000, (d / 100) % 100);
        return true;
    }
    if (!strcmp(name, "unix")) { snprintf(out, cap, "%lld", (long long)now); return true; }
    if (!strcmp(name, "doy"))  { snprintf(out, cap, "%d", t.tm_yday + 1); return true; }
    if (!strcmp(name, "week")) { strftime(out, cap, "%V", &t); return true; }
    if (!strcmp(name, "bar")) {
        bar(fraction(arg && *arg ? arg : "day", &t, sec), 10, out, cap);
        return true;
    }
    if (!strcmp(name, "pct")) {
        snprintf(out, cap, "%d", (int)(fraction(arg && *arg ? arg : "day", &t, sec) * 100));
        return true;
    }
    if (!strcmp(name, "moon"))    { snprintf(out, cap, "%s", moon_name(moon_age(now))); return true; }
    if (!strcmp(name, "moonpct")) {
        double age = moon_age(now);
        snprintf(out, cap, "%d", (int)((1 - cos(2 * M_PI * age / 29.530588853)) / 2 * 100 + 0.5));
        return true;
    }
    if (!strcmp(name, "sunrise")) { hm12(sun_event(&t, now, true), out, cap);  return true; }
    if (!strcmp(name, "sunset"))  { hm12(sun_event(&t, now, false), out, cap); return true; }
    if (!strcmp(name, "tz") && arg) return city_time(arg, now, out, cap);
    if (!strcmp(name, "until") && arg) return until_value(arg, &t, out, cap);
    return false;
}
