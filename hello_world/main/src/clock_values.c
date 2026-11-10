/* Time-based live values for scene text: {clock}, {date}, {fuzzy}, {roman},
 * {moon}, {sunset}, {tz:tyo}, {until:12-25}, {bar:day} ... (docs/live-values.md).
 *
 * Pure C (see clock_values.h): everything is computed from the instant and
 * the local UTC offset handed in, with our own calendar arithmetic instead of
 * localtime_r(), mktime() or strftime(). That keeps the globe independent of
 * the C library's time-zone handling and lets hello_world/host_test compile
 * this exact file on a PC and compare it with web/src/lib/values.js.
 * Only {sunrise}/{sunset} need the location from settings.json. */

#include "clock_values.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- calendar arithmetic (proleptic Gregorian, no time zones involved) ---- */

static int64_t floor_div(int64_t a, int64_t b) { return a / b - (a % b != 0 && (a < 0) != (b < 0)); }

/* Days since 1970-01-01 of y-m-d (m 1-12). A day past the month's end rolls
 * into the next month, as mktime() and JavaScript's Date do. */
static int64_t days_from_civil(int64_t y, int m, int d)
{
    y -= m <= 2;
    int64_t era = floor_div(y, 400), yoe = y - era * 400;
    int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = floor_div(z, 146097), doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

static int weekday(int64_t days) { return (int)((days % 7 + 11) % 7); }   // 1970-01-01 was a Thursday

static bool leap(int y) { return (y % 4 == 0 && y % 100) || y % 400 == 0; }

static int month_days(int y, int m)
{
    static const int dm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return dm[m - 1] + (m == 2 && leap(y));
}

void clock_now_set(clock_now_t *c, int64_t utc, int32_t usec, int32_t offset)
{
    c->utc = utc; c->usec = usec; c->offset = offset;
    int64_t local = utc + offset, days = floor_div(local, 86400), sod = local - days * 86400;
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    struct tm *t = &c->tm;
    memset(t, 0, sizeof(*t));
    t->tm_year = y - 1900; t->tm_mon = m - 1; t->tm_mday = d;
    t->tm_hour = (int)(sod / 3600); t->tm_min = (int)(sod / 60 % 60); t->tm_sec = (int)(sod % 60);
    t->tm_wday = weekday(days);
    t->tm_yday = (int)(days - days_from_civil(y, 1, 1));
}

int64_t clock_tm_seconds(const struct tm *t)
{
    int64_t y = t->tm_year + 1900 + floor_div(t->tm_mon, 12);
    int m = (int)(t->tm_mon - floor_div(t->tm_mon, 12) * 12) + 1;
    return days_from_civil(y, m, t->tm_mday) * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

/* ---- words and numbers ---- */

static const char *ONES[] = { "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN",
    "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN", "SEVENTEEN", "EIGHTEEN", "NINETEEN" };
static const char *TENS[] = { "", "", "TWENTY", "THIRTY", "FORTY", "FIFTY" };
static const char *DAYS[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *MONTHS[] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                "September", "October", "November", "December" };

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
    int y = t->tm_year + 1900;
    if (!strcmp(what, "min"))   return sec / 60.0;
    if (!strcmp(what, "hour"))  return (t->tm_min * 60 + sec) / 3600.0;
    if (!strcmp(what, "day"))   return day;
    if (!strcmp(what, "week"))  return (((t->tm_wday + 6) % 7) + day) / 7.0;
    if (!strcmp(what, "month")) return (t->tm_mday - 1 + day) / month_days(y, t->tm_mon + 1);
    if (!strcmp(what, "year"))  return (t->tm_yday + day) / (leap(y) ? 366 : 365);
    return 0;
}

/* ISO 8601 week: the week's Thursday decides which year it belongs to. */
static int iso_week(const struct tm *t)
{
    int64_t day = days_from_civil(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    int64_t thu = day - (t->tm_wday + 6) % 7 + 3;
    int y, m, d;
    civil_from_days(thu, &y, &m, &d);
    return (int)((thu - days_from_civil(y, 1, 1)) / 7 + 1);
}

/* ---- colon blink ---- */

/* Periods under 200 ms count as steady, as the engine reads "blinkColon". The
 * "on" half is compared as 2 * phase < period so odd periods split exactly
 * like the preview's ms % period < period / 2. */
static void colon_blink(char *s, const clock_now_t *c, unsigned period, char look)
{
    if (c->steady || period < 200) return;
    int64_t ms = (c->utc % 3600) * 1000 + c->usec / 1000;
    if (2 * (ms % period) < (int64_t)period) return;
    for (; *s; s++) if (*s == ':') *s = look;
}

/* ---- sky ---- */

/* Moon age in days since the last new moon (mean synodic month). From whole
 * milliseconds, exactly as the preview computes it from Date.getTime(). */
static double moon_age(const clock_now_t *c)
{
    double jd = (double)(c->utc * 1000 + c->usec / 1000) / 864e5 + 2440587.5;
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

/* Sunrise/sunset (NOAA almanac algorithm), local hours or NAN (polar day/night). */
static double sun_event(const clock_now_t *c, bool rise)
{
    if (isnan(s_lat)) return NAN;
    const double D = M_PI / 180;
    int N = c->tm.tm_yday + 1;
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
    return fmod(UT + c->offset / 3600.0 + 48, 24);
}

static void hm12(double hours, char *out, size_t cap)
{
    if (isnan(hours)) { snprintf(out, cap, "--:--"); return; }
    int m = (int)floor(hours * 60 + 0.5) % 1440;
    snprintf(out, cap, "%d:%02d", h12(m / 60), m % 60);
}

/* ---- world clock ----
 * Each city's standard offset and daylight-saving rule, evaluated from UTC.
 * (Swapping the process-wide TZ and calling tzset() would give every other
 * task the wrong zone for that moment, and setenv() allocates each time.)
 * Rules are those of the POSIX TZ strings in the comments; the web app uses
 * the matching IANA zones. If a country changes its rules, update both. */

#define HR 3600

/* "Mm.w.d/time": month, week of the month (5 = last), weekday (0 = Sunday),
 * local time of the change (standard time when DST starts, DST when it ends). */
typedef struct { int8_t mon, week, wday; int32_t at; } dst_rule_t;
#define NO_DST { 0, 0, 0, 0 }

static const struct {
    const char *key;
    int32_t     std;            // standard offset east of UTC, seconds
    dst_rule_t  start, end;     // start.mon == 0: no DST; DST is std + 1 h
} CITIES[] = {
    { "nyc", -5 * HR, { 3, 2, 0, 2 * HR }, { 11, 1, 0, 2 * HR } },     // EST5EDT,M3.2.0,M11.1.0
    { "chi", -6 * HR, { 3, 2, 0, 2 * HR }, { 11, 1, 0, 2 * HR } },     // CST6CDT,M3.2.0,M11.1.0
    { "den", -7 * HR, { 3, 2, 0, 2 * HR }, { 11, 1, 0, 2 * HR } },     // MST7MDT,M3.2.0,M11.1.0
    { "la",  -8 * HR, { 3, 2, 0, 2 * HR }, { 11, 1, 0, 2 * HR } },     // PST8PDT,M3.2.0,M11.1.0
    { "hnl", -10 * HR, NO_DST, NO_DST },                               // HST10
    { "lon", 0,       { 3, 5, 0, 1 * HR }, { 10, 5, 0, 2 * HR } },     // GMT0BST,M3.5.0/1,M10.5.0
    { "par", 1 * HR,  { 3, 5, 0, 2 * HR }, { 10, 5, 0, 3 * HR } },     // CET-1CEST,M3.5.0,M10.5.0/3
    { "ber", 1 * HR,  { 3, 5, 0, 2 * HR }, { 10, 5, 0, 3 * HR } },     // CET-1CEST,M3.5.0,M10.5.0/3
    { "ath", 2 * HR,  { 3, 5, 0, 3 * HR }, { 10, 5, 0, 4 * HR } },     // EET-2EEST,M3.5.0/3,M10.5.0/4
    { "dxb", 4 * HR,   NO_DST, NO_DST },                               // <+04>-4
    { "del", 5 * HR + 30 * 60, NO_DST, NO_DST },                       // IST-5:30
    { "sgp", 8 * HR,   NO_DST, NO_DST },                               // <+08>-8
    { "hkg", 8 * HR,   NO_DST, NO_DST },                               // HKT-8
    { "tyo", 9 * HR,   NO_DST, NO_DST },                               // JST-9
    { "syd", 10 * HR, { 10, 1, 0, 2 * HR }, { 4, 1, 0, 3 * HR } },     // AEST-10AEDT,M10.1.0,M4.1.0/3
    { "akl", 12 * HR, { 9, 5, 0, 2 * HR },  { 4, 1, 0, 3 * HR } },     // NZST-12NZDT,M9.5.0,M4.1.0/3
    { "utc", 0,        NO_DST, NO_DST },                               // UTC0
};

/* Day number of the rule's date in year y. */
static int64_t rule_day(int y, const dst_rule_t *r)
{
    int64_t first = days_from_civil(y, r->mon, 1);
    int64_t day = first + (r->wday - weekday(first) + 7) % 7 + 7 * (r->week - 1);
    while (day >= first + month_days(y, r->mon)) day -= 7;     // week 5 = the last one
    return day;
}

bool clock_city_offset(const char *key, int64_t utc, int32_t *offset)
{
    for (size_t i = 0; i < sizeof(CITIES) / sizeof(CITIES[0]); i++) {
        if (strcmp(key, CITIES[i].key)) continue;
        int32_t std = CITIES[i].std;
        const dst_rule_t *on = &CITIES[i].start, *off = &CITIES[i].end;
        *offset = std;
        if (!on->mon) return true;
        int y, m, d;                        // the year in local standard time (no change is near New Year)
        civil_from_days(floor_div(utc + std, 86400), &y, &m, &d);
        int64_t t_on  = rule_day(y, on) * 86400 + on->at - std;
        int64_t t_off = rule_day(y, off) * 86400 + off->at - (std + HR);
        bool dst = t_on < t_off ? (utc >= t_on && utc < t_off)      // northern summer
                                : !(utc >= t_off && utc < t_on);    // southern: DST spans New Year
        if (dst) *offset += HR;
        return true;
    }
    return false;
}

static bool city_time(const char *key, int64_t utc, char *out, size_t cap)
{
    int32_t off;
    if (!clock_city_offset(key, utc, &off)) return false;
    int64_t s = utc + off, sod = s - floor_div(s, 86400) * 86400;
    snprintf(out, cap, "%d:%02d", h12((int)(sod / 3600)), (int)(sod / 60 % 60));
    return true;
}

/* ---- countdowns ---- */

/* Days since 1970 of y-mon-day with the month allowed outside 1-12 (13 = next January). */
static int64_t days_norm(int64_t y, int mon, int day)
{
    y += floor_div(mon - 1, 12);
    mon = (int)((mon - 1) - floor_div(mon - 1, 12) * 12) + 1;
    return days_from_civil(y, mon, day);
}

/* The whole of s is 1-2 digits, sep, then min_b-2 digits (the preview's
 * /^(\d{1,2}):(\d{2})$/ and /^(\d{1,2})-(\d{1,2})$/). */
static bool two_numbers(const char *s, char sep, int min_b, int *a, int *b)
{
    int n = 0;
    *a = *b = 0;
    for (; s[n] >= '0' && s[n] <= '9' && n < 3; n++) *a = *a * 10 + (s[n] - '0');
    if (n < 1 || n > 2 || s[n] != sep) return false;
    s += n + 1;
    for (n = 0; s[n] >= '0' && s[n] <= '9' && n < 3; n++) *b = *b * 10 + (s[n] - '0');
    return n >= min_b && n <= 2 && s[n] == '\0';
}

/* {until:HH:MM} -> "h:mm" to the next such time; {until:MM-DD} -> days to the next such date.
 * Both count on the wall clock, so on a DST change day the time is off by the hour. */
static bool until_value(const char *arg, const struct tm *t, char *out, size_t cap)
{
    int a, b;
    if (two_numbers(arg, ':', 2, &a, &b)) {
        int now_m = t->tm_hour * 60 + t->tm_min, tgt = a * 60 + b;
        int d = (tgt - now_m + 1440) % 1440;
        snprintf(out, cap, "%d:%02d", d / 60, d % 60);
        return true;
    }
    if (two_numbers(arg, '-', 1, &a, &b)) {
        int y = t->tm_year + 1900;
        int64_t today = days_from_civil(y, t->tm_mon + 1, t->tm_mday), tgt = days_norm(y, a, b);
        if (tgt < today) tgt = days_norm(y + 1, a, b);
        snprintf(out, cap, "%lld", (long long)(tgt - today));
        return true;
    }
    return false;
}

/* ---- the values ---- */

bool clock_value(const char *name, const char *arg, const clock_now_t *c, char *out, size_t cap)
{
    const struct tm *t = &c->tm;
    double sec = t->tm_sec + c->usec / 1e6;
    char a[24], b[24];

    if (!strcmp(name, "clock")) {             // the classic clock: "07:05 PM", colon blinks
        // {clock:24} 24 h, {clock:s} seconds, {clock:x} no AM/PM, {clock:n} no blink (combinable)
        bool h24 = arg && strchr(arg, '2'), secs = arg && strchr(arg, 's'), noblink = arg && strchr(arg, 'n');
        bool ampm = !h24 && !(arg && strchr(arg, 'x'));
        if (secs) snprintf(a, sizeof(a), ":%02d", t->tm_sec); else a[0] = '\0';
        snprintf(out, cap, "%02d:%02d%s%s", h24 ? t->tm_hour : h12(t->tm_hour), t->tm_min, a,
                 ampm ? (t->tm_hour < 12 ? " AM" : " PM") : "");
        if (c->colon_set) colon_blink(out, c, c->colon_period, c->colon_look);
        else colon_blink(out, c, noblink || secs ? 0 : 1000, ' ');
        return true;
    }
    bool h24 = !strcmp(name, "time24");
    if (h24 || !strcmp(name, "time")) {       // "19:05", "7:05"; blink only with the frame's setting
        snprintf(out, cap, h24 ? "%02d:%02d" : "%d:%02d", h24 ? t->tm_hour : h12(t->tm_hour), t->tm_min);
        if (c->colon_set) colon_blink(out, c, c->colon_period, c->colon_look);
        return true;
    }
    if (!strcmp(name, "ampm"))  { snprintf(out, cap, "%s", t->tm_hour < 12 ? "AM" : "PM"); return true; }
    if (!strcmp(name, "sec"))   { snprintf(out, cap, "%02d", t->tm_sec); return true; }
    if (!strcmp(name, "date"))  { snprintf(out, cap, "%.3s %02d %.3s", DAYS[t->tm_wday], t->tm_mday, MONTHS[t->tm_mon]); return true; }
    if (!strcmp(name, "day"))   { snprintf(out, cap, "%s", DAYS[t->tm_wday]); return true; }
    if (!strcmp(name, "dd"))    { snprintf(out, cap, "%02d", t->tm_mday); return true; }
    if (!strcmp(name, "month")) { snprintf(out, cap, "%s", MONTHS[t->tm_mon]); return true; }
    if (!strcmp(name, "mon"))   { snprintf(out, cap, "%.3s", MONTHS[t->tm_mon]); return true; }
    if (!strcmp(name, "year"))  { snprintf(out, cap, "%d", t->tm_year + 1900); return true; }

    if (!strcmp(name, "words")) {                       // "TEN TWENTY EIGHT", "TEN O'CLOCK", "TEN OH FIVE"
        number_words(h12(t->tm_hour), a, sizeof(a));
        if (t->tm_min == 0) snprintf(out, cap, "%s O'CLOCK", a);
        else if (t->tm_min < 10) { number_words(t->tm_min, b, sizeof(b)); snprintf(out, cap, "%s OH %s", a, b); }
        else { number_words(t->tm_min, b, sizeof(b)); snprintf(out, cap, "%s %s", a, b); }
        return true;
    }
    if (!strcmp(name, "fuzzy")) {                       // to the nearest five minutes
        int m5 = ((t->tm_min * 60 + t->tm_sec + 150) / 300) * 5;
        int h = t->tm_hour + (m5 > 30 ? 1 : 0);
        if (m5 == 60) { m5 = 0; h = t->tm_hour + 1; }
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
        roman(h12(t->tm_hour), a, sizeof(a));
        roman(t->tm_min, b, sizeof(b));
        snprintf(out, cap, "%s:%s", a, b);
        return true;
    }
    if (!strcmp(name, "binary")) {
        for (int i = 0; i < 5; i++) a[i] = (t->tm_hour >> (4 - i)) & 1 ? '1' : '0';
        a[5] = '\0';
        for (int i = 0; i < 6; i++) b[i] = (t->tm_min >> (5 - i)) & 1 ? '1' : '0';
        b[6] = '\0';
        snprintf(out, cap, "%s:%s", a, b);
        return true;
    }
    if (!strcmp(name, "beat")) {                         // Swatch internet time, Biel (UTC+1)
        long s = (long)((c->utc + 3600) % 86400);
        snprintf(out, cap, "%03ld", s * 10 / 864);
        return true;
    }
    if (!strcmp(name, "decimal")) {                      // 10-hour day, 100-minute hour
        long long d = (long long)(t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec) * 100000LL / 86400;
        snprintf(out, cap, "%lld:%02lld", d / 10000, (d / 100) % 100);
        return true;
    }
    if (!strcmp(name, "unix")) { snprintf(out, cap, "%lld", (long long)c->utc); return true; }
    if (!strcmp(name, "doy"))  { snprintf(out, cap, "%d", t->tm_yday + 1); return true; }
    if (!strcmp(name, "week")) { snprintf(out, cap, "%02d", iso_week(t)); return true; }
    if (!strcmp(name, "bar")) {
        bar(fraction(arg && *arg ? arg : "day", t, sec), 10, out, cap);
        return true;
    }
    if (!strcmp(name, "pct")) {
        snprintf(out, cap, "%d", (int)(fraction(arg && *arg ? arg : "day", t, sec) * 100));
        return true;
    }
    if (!strcmp(name, "moon"))    { snprintf(out, cap, "%s", moon_name(moon_age(c))); return true; }
    if (!strcmp(name, "moonpct")) {
        double age = moon_age(c);
        snprintf(out, cap, "%d", (int)((1 - cos(2 * M_PI * age / 29.530588853)) / 2 * 100 + 0.5));
        return true;
    }
    if (!strcmp(name, "sunrise")) { hm12(sun_event(c, true), out, cap);  return true; }
    if (!strcmp(name, "sunset"))  { hm12(sun_event(c, false), out, cap); return true; }
    if (!strcmp(name, "tz") && arg) return city_time(arg, c->utc, out, cap);
    if (!strcmp(name, "until") && arg) return until_value(arg, t, out, cap);
    return false;
}
