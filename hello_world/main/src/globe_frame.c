#include "globe_frame.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---- Character set (docs/protocol.md, "Character set" and "Second byte") ---- */

/* Printable ASCII the font lacks; the globe draws them as blanks. */
static bool ascii_missing(uint32_t c)
{
    return c && strchr("%&+;@[\\]^_`{|}~", (int)c) != NULL;
}

/* Accented letters live in the control codes 0x01-0x1A. */
static const struct { uint32_t cp; uint8_t code; } k_accents[] = {
    {0xE0, 0x01}, {0xE1, 0x02}, {0xE2, 0x03}, {0xE4, 0x04},   // a grave/acute/circumflex/diaeresis
    {0xE8, 0x05}, {0xE9, 0x06}, {0xEA, 0x07}, {0xEB, 0x08},   // e
    {0xEC, 0x09}, {0xED, 0x0A}, {0xEE, 0x0B}, {0xEF, 0x0C},   // i
    {0xF2, 0x0D}, {0xF3, 0x0E}, {0xF4, 0x0F}, {0xF6, 0x10},   // o
    {0xF9, 0x11}, {0xFA, 0x12}, {0xFB, 0x13}, {0xFC, 0x14},   // u
    {0xC7, 0x15}, {0xF1, 0x16}, {0xC9, 0x1A},                 // C cedilla, n tilde, E acute
    {0x2709, 0x80}, {0x1F4E7, 0x80}, {0x1F4E9, 0x80},          // envelope icons -> 0x80
};

static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = s[0];
    int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
    if (extra < 0) { (*p)++; return 0xFFFD; }
    if (extra) c &= (0x3F >> extra);
    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p += i; return 0xFFFD; }
        c = (c << 6) | (s[i] & 0x3F);
    }
    *p += extra + 1;
    return c;
}

size_t globe_text_encode(const char *utf8, uint8_t *out, size_t cap, int *unsupported)
{
    size_t n = 0;
    int bad = 0;
    const char *p = utf8 ? utf8 : "";
    while (*p && n < cap) {
        uint32_t c = utf8_next(&p);
        uint8_t code = ' ';
        if (c >= 0x20 && c < 0x7F && !ascii_missing(c)) {
            code = (uint8_t)c;
        } else {
            bool found = false;
            for (size_t i = 0; i < sizeof(k_accents) / sizeof(k_accents[0]); i++) {
                if (k_accents[i].cp == c) { code = k_accents[i].code; found = true; break; }
            }
            if (!found) bad++;
        }
        out[n++] = code;
    }
    if (unsupported) *unsupported = bad;
    return n;
}

/* ---- Effects ---- */

int globe_effect_lookup(const char *s)
{
    if (!s || !*s) return -1;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end == '\0') return (v >= 0 && v < g_globe_effect_count) ? (int)v : -1;
    for (int i = 0; i < g_globe_effect_count; i++) {
        const char *a = g_globe_effects[i].name, *b = s;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (*a == '\0' && *b == '\0') return g_globe_effects[i].id;
    }
    return -1;
}

/* ---- Frames (docs/protocol.md, "Frame formats") ---- */

static size_t put_text(uint8_t *dst, uint8_t cmd, const uint8_t *text, size_t len)
{
    dst[0] = cmd;
    dst[1] = 0x00;                 // byte 2 is really a leading NUL character
    memcpy(dst + 2, text, len);
    return 2 + len;
}

static size_t still_frame(uint8_t *dst, const uint8_t *text, size_t len, const globe_style_t *st)
{
    size_t n;
    switch (st->after) {
    case GLOBE_AFTER_FLASH: {
        // Whole text blinks: a still frame whose blink byte covers every
        // character (len + 1). Command 02 only flashed part of longer text.
        int shift = st->shift >= 0 ? st->shift : (int)len;
        if (shift > 255) shift = 255;
        n = put_text(dst, 0x05, text, len);
        dst[n++] = 0x00;
        dst[n++] = (uint8_t)(len + 1);
        dst[n++] = (uint8_t)shift;
        return n;
    }
    case GLOBE_AFTER_INSTANT:
        n = put_text(dst, 0x01, text, len);
        dst[n++] = 0x00;
        return n;
    case GLOBE_AFTER_HOLD:
    default: {
        int blink = st->blink_chars > 0 ? st->blink_chars + 1 : 0;   // byte n blinks n-1 chars
        if (blink > (int)len + 1) blink = (int)len + 1;
        int shift = st->shift >= 0 ? st->shift : (int)len;           // clock convention: centred
        if (shift > 255) shift = 255;
        n = put_text(dst, 0x05, text, len);
        dst[n++] = 0x00;
        dst[n++] = (uint8_t)blink;
        dst[n++] = (uint8_t)shift;
        return n;
    }
    }
}

bool globe_frame_compile(const uint8_t *text, size_t len, const globe_style_t *st,
                         uint8_t *first, size_t *first_len,
                         uint8_t *second, size_t *second_len)
{
    *first_len = 0;
    *second_len = 0;
    if (!st || len > GLOBE_TEXT_MAX) return false;
    if (st->entrance >= g_globe_effect_count) return false;

    if (st->entrance >= 0) {
        size_t n = put_text(first, 0x00, text, len);
        first[n++] = (uint8_t)st->entrance;
        *first_len = n;
        if (st->after != GLOBE_AFTER_SPIN) {
            *second_len = still_frame(second, text, len, st);
        }
        return true;
    }
    if (st->after == GLOBE_AFTER_SPIN) {
        size_t n = put_text(first, 0x04, text, len);   // show at once and spin
        first[n++] = 0x00;
        *first_len = n;
        return true;
    }
    *first_len = still_frame(first, text, len, st);
    return true;
}
