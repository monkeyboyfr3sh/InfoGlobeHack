#include "globe_anim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Mirrored line for line by web/src/lib/anim.js; change both together and
 * regenerate hello_world/test_host/anim_vectors.txt (see run.sh there).
 * Integer arithmetic only (no sin/pow), so both sides round identically. */

static const char *const k_types[GA_TYPES] = {
    "none", "decode", "typewriter", "count", "bounce", "scanner", "sprites", "automaton", "sequence",
};
static const uint16_t k_ms[GA_TYPES] = { 0, 110, 140, 70, 100, 110, 150, 400, 400 };

const char *ga_type_name(int type) { return type > 0 && type < GA_TYPES ? k_types[type] : "none"; }

static int pick(const char *v, const char *const *names, int n)
{
    for (int i = 0; i < n; i++) if (!strcmp(v, names[i])) return i;
    return -1;
}

/* Printable ASCII the globe can draw (docs/protocol.md, "Character set"). */
static bool drawable(int c)
{
    return c >= 0x20 && c < 0x7F && !strchr("%&+;@[\\]^_`{|}~", c);
}

bool ga_init(ga_anim_t *a, const char *type)
{
    memset(a, 0, sizeof(*a));
    int t = type ? pick(type, k_types, GA_TYPES) : -1;
    if (t <= 0) return false;
    a->type = (uint8_t)t;
    a->ms = k_ms[t];
    a->lead = 4;
    a->steps = 12;
    a->period = 3000;
    a->pause = 100;
    a->width = t == GA_AUTOMATON ? 35 : 10;
    a->rule = 30;
    a->gens = 120;
    a->loop = true;
    a->cursor = '#';
    a->bg = ' ';
    a->live = '#';
    a->dead = ' ';
    return true;
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

void ga_set_num(ga_anim_t *a, const char *key, double v)
{
    if (!a || !key || !isfinite(v)) return;
    if      (!strcmp(key, "ms"))     a->ms = (uint16_t)clampd(v, GA_MS_MIN, GA_MS_MAX);
    else if (!strcmp(key, "lead"))   a->lead = (uint8_t)clampd(v, 0, 30);
    else if (!strcmp(key, "steps"))  a->steps = (uint8_t)clampd(v, 1, 60);
    else if (!strcmp(key, "period")) a->period = (uint16_t)clampd(v, 400, 20000);
    else if (!strcmp(key, "pause"))  a->pause = (uint16_t)clampd(v, 0, 5000);
    else if (!strcmp(key, "amp"))    a->amp = (uint8_t)clampd(v, 0, 60);
    else if (!strcmp(key, "width"))  a->width = (uint8_t)clampd(v, 3, GA_CELLS_MAX);
    else if (!strcmp(key, "rule"))   a->rule = (uint8_t)clampd(v, 0, 255);
    else if (!strcmp(key, "gens"))   a->gens = (uint8_t)clampd(v, 2, 250);
    else if (!strcmp(key, "seed"))   a->seed = (uint32_t)clampd(v, 0, 4294967295.0);
    else if (!strcmp(key, "loop"))   a->loop = v != 0;
}

void ga_set_str(ga_anim_t *a, const char *key, const char *v)
{
    static const char *const orders[] = { "lr", "rl", "random" };
    static const char *const noises[] = { "match", "symbols", "mixed" };
    static const char *const modes[] = { "number", "digits" };
    static const char *const sprites[] = { "pacman", "chase", "pulse", "fish" };
    static const char *const starts[] = { "single", "random" };
    if (!a || !key || !v) return;
    bool one = v[0] && !v[1] && drawable(v[0]);       // a single glyph the globe has
    int i;
    if      (!strcmp(key, "order"))  { if ((i = pick(v, orders, 3)) >= 0) a->order = (uint8_t)i; }
    else if (!strcmp(key, "noise"))  { if ((i = pick(v, noises, 3)) >= 0) a->noise = (uint8_t)i; }
    else if (!strcmp(key, "mode"))   { if ((i = pick(v, modes, 2)) >= 0) a->mode = (uint8_t)i; }
    else if (!strcmp(key, "sprite")) { if ((i = pick(v, sprites, 4)) >= 0) a->sprite = (uint8_t)i; }
    else if (!strcmp(key, "start"))  { if ((i = pick(v, starts, 2)) >= 0) a->start = (uint8_t)i; }
    else if (!strcmp(key, "cursor")) { if (!v[0] || !strcmp(v, "none")) a->cursor = 0; else if (one) a->cursor = v[0]; }
    else if (!strcmp(key, "bg"))     { if (one) a->bg = v[0]; }
    else if (!strcmp(key, "live"))   { if (one) a->live = v[0]; }
    else if (!strcmp(key, "dead"))   { if (one) a->dead = v[0]; }
    else if (!strcmp(key, "trail")) {
        size_t n = strlen(v);
        bool ok = n < sizeof(a->trail);
        for (size_t j = 0; ok && j < n; j++) ok = drawable(v[j]);
        if (ok) memcpy(a->trail, v, n + 1);
    }
}

size_t ga_add_text(ga_anim_t *a, char *pool, size_t room, const char *text)
{
    if (!a || !pool || !text || a->ntexts >= GA_SEQ_MAX) return 0;
    size_t n = strlen(text);
    if (n > GA_SEQ_TEXT_MAX) {
        n = GA_SEQ_TEXT_MAX;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;   // don't split a character
    }
    if (n + 1 > room) return 0;
    memcpy(pool, text, n);
    pool[n] = '\0';
    if (!a->ntexts) a->texts = pool;
    a->ntexts++;
    return n + 1;
}

bool ga_finite(const ga_anim_t *a)
{
    switch (a->type) {
    case GA_DECODE: case GA_TYPEWRITER: case GA_COUNT: return true;
    case GA_SEQUENCE: return !a->loop || !a->ntexts;
    default: return false;
    }
}

uint32_t ga_airtime_ms(size_t cells, unsigned gap_ms)
{
    return (uint32_t)(cells + GA_FRAME_EXTRA) * 8 + gap_ms;
}

/* ---- Text as cells (one per code point) ---- */

typedef struct {
    const char *s;
    int         n;
    uint8_t     off[GA_CELLS_MAX], len[GA_CELLS_MAX];
    uint32_t    cp[GA_CELLS_MAX];
} cells_t;

static int utf8_len(const unsigned char *p)
{
    unsigned c = p[0];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    for (int i = 1; i < n; i++) if ((p[i] & 0xC0) != 0x80) return i;   // cut short: stop at the bad byte
    return n;
}

static void split(const char *s, cells_t *c)
{
    c->s = s ? s : "";
    c->n = 0;
    size_t o = 0;
    while (c->s[o] && c->n < GA_CELLS_MAX) {
        const unsigned char *p = (const unsigned char *)c->s + o;
        int l = utf8_len(p);
        uint32_t cp = p[0];
        if (l > 1) {
            cp &= 0x3Fu >> (l - 1);
            for (int i = 1; i < l; i++) cp = (cp << 6) | (p[i] & 0x3F);
        }
        c->off[c->n] = (uint8_t)o;
        c->len[c->n] = (uint8_t)l;
        c->cp[c->n] = cp;
        c->n++;
        o += (size_t)l;
    }
}

typedef struct { char *b; size_t n; int cells; } out_t;

static void put(out_t *o, const char *p, size_t len)
{
    if (o->cells >= GA_CELLS_MAX || o->n + len >= GA_TEXT_BYTES) return;
    memcpy(o->b + o->n, p, len);
    o->n += len;
    o->b[o->n] = '\0';
    o->cells++;
}
static void put_ch(out_t *o, char c) { put(o, &c, 1); }
static void put_cell(out_t *o, const cells_t *c, int i) { put(o, c->s + c->off[i], c->len[i]); }
static void put_all(out_t *o, const cells_t *c) { for (int i = 0; i < c->n; i++) put_cell(o, c, i); }

/* ---- Deterministic randomness: a stateless hash, so any step can be computed alone ---- */

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static uint32_t rnd(uint32_t seed, uint32_t a, uint32_t b) { return mix(seed ^ mix(a + mix(b + 0x9e3779b9u))); }

static uint32_t text_seed(const ga_anim_t *a, const cells_t *t)
{
    uint32_t h = 2166136261u;                            // FNV-1a over code points
    for (int i = 0; i < t->n; i++) { h ^= t->cp[i]; h *= 16777619u; }
    return mix(h ^ ((uint32_t)a->type * 0x9e3779b9u) ^ a->seed);
}

/* ---- The animations ---- */

static const char UPPER[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char LOWER[] = "abcdefghijklmnopqrstuvwxyz";
static const char DIGIT[] = "0123456789";
static const char SYMBOL[] = "#$*<>=?/";
static const char MIXED[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$*<>=?";

/* Noise that locks into the text, a character (or a few, for long text) per step. */
static bool decode(const ga_anim_t *a, const cells_t *t, uint32_t k, out_t *o)
{
    int n = t->n, span = n < 16 ? n : 16;
    uint8_t rank[GA_CELLS_MAX];
    for (int i = 0; i < n; i++) rank[i] = (uint8_t)(a->order == GA_ORDER_RL ? n - 1 - i : i);
    if (a->order == GA_ORDER_RANDOM) {
        // seeded without the text, so the order holds still while a colon blinks
        uint8_t pos[GA_CELLS_MAX];
        uint32_t s = mix(a->seed ^ ((uint32_t)n * 0x85ebca6bu));
        for (int i = 0; i < n; i++) pos[i] = (uint8_t)i;
        for (int j = n - 1; j > 0; j--) {
            int r = (int)(rnd(s, 0x5eed, (uint32_t)j) % (uint32_t)(j + 1));
            uint8_t x = pos[j]; pos[j] = pos[r]; pos[r] = x;
        }
        for (int r = 0; r < n; r++) rank[pos[r]] = (uint8_t)r;
    }
    uint32_t seed = text_seed(a, t);
    for (int i = 0; i < n; i++) {
        uint32_t cp = t->cp[i];
        const char *set = NULL;
        if (cp != ' ') {
            if (a->noise == GA_NOISE_SYMBOLS) set = SYMBOL;
            else if (a->noise == GA_NOISE_MIXED) set = MIXED;
            else if (cp >= 'A' && cp <= 'Z') set = UPPER;
            else if (cp >= 'a' && cp <= 'z') set = LOWER;
            else if (cp >= '0' && cp <= '9') set = DIGIT;
        }
        uint32_t lock = a->lead + (uint32_t)rank[i] * (uint32_t)span / (uint32_t)n;
        if (!set || k >= lock) put_cell(o, t, i);
        else put_ch(o, set[rnd(seed, k, (uint32_t)i) % strlen(set)]);
    }
    return n == 0 || k + 1 >= (uint32_t)a->lead + (uint32_t)span;
}

/* One more character per step; padded with spaces so the text never moves. */
static bool typewriter(const ga_anim_t *a, const cells_t *t, uint32_t k, out_t *o)
{
    for (int i = 0; i < t->n; i++) {
        if ((uint32_t)i < k) put_cell(o, t, i);
        else if ((uint32_t)i == k && a->cursor) put_ch(o, a->cursor);
        else put_ch(o, ' ');
    }
    return k >= (uint32_t)t->n;
}

typedef struct { int start, len; } run_t;

static int digit_runs(const cells_t *c, run_t *r)
{
    int m = 0;
    for (int i = 0; i < c->n;) {
        if (c->cp[i] < '0' || c->cp[i] > '9') { i++; continue; }
        int j = i;
        while (j < c->n && c->cp[j] >= '0' && c->cp[j] <= '9') j++;
        r[m].start = i;
        r[m].len = j - i;
        m++;
        i = j;
    }
    return m;
}

static int64_t run_value(const cells_t *c, const run_t *r)
{
    int64_t v = 0;
    for (int i = 0; i < r->len; i++) v = v * 10 + (int64_t)(c->cp[r->start + i] - '0');
    return v;
}

/* Every number rolls from its old value (or 0) to the new one: counting with an
 * ease-out, or each digit turning forward like an odometer wheel. */
static bool count(const ga_anim_t *a, const ga_input_t *in, const cells_t *t, uint32_t k, out_t *o)
{
    run_t tr[GA_CELLS_MAX / 2 + 1], fr[GA_CELLS_MAX / 2 + 1];
    cells_t f;
    split(in->from, &f);
    int nt = digit_runs(t, tr), nf = digit_runs(&f, fr), counted = 0;
    int64_t S = a->steps, j = (int64_t)k + 1 < S ? (int64_t)k + 1 : S;
    char ch[GA_CELLS_MAX];
    bool over[GA_CELLS_MAX] = { false };
    for (int r = 0; r < nt; r++) {
        int w = tr[r].len, s0 = tr[r].start;
        if (w > 9) continue;                                     // too long to be a number: as it is
        counted++;
        int64_t V = run_value(t, &tr[r]);
        int64_t F = r < nf && fr[r].len <= 9 ? run_value(&f, &fr[r]) : 0;
        if (a->mode == GA_COUNT_DIGITS) {
            for (int d = w - 1; d >= 0; d--) {                  // right-aligned, like wheels
                int64_t b = t->cp[s0 + d] - '0', a0 = F % 10, dist = (b - a0 + 10) % 10;
                F /= 10;
                ch[s0 + d] = (char)('0' + (a0 + dist * j / S) % 10);
                over[s0 + d] = true;
            }
        } else {
            int64_t D3 = S * S * S, rest = S - j, e = D3 - rest * rest * rest;   // ease-out cubic
            int64_t v = V >= F ? F + ((V - F) * e * 2 + D3) / (2 * D3) : F - ((F - V) * e * 2 + D3) / (2 * D3);
            int64_t cap = 1;
            for (int i = 0; i < w; i++) cap *= 10;
            if (v > cap - 1) v = cap - 1;
            bool zero = w > 1 && t->cp[s0] == '0';             // "05" keeps its leading zero
            for (int d = w - 1; d >= 0; d--) {
                if (d == w - 1 || v > 0) { ch[s0 + d] = (char)('0' + v % 10); v /= 10; }
                else ch[s0 + d] = zero ? '0' : ' ';
                over[s0 + d] = true;
            }
        }
    }
    for (int i = 0; i < t->n; i++) {
        if (over[i]) put_ch(o, ch[i]);
        else put_cell(o, t, i);
    }
    return counted == 0 || (int64_t)k + 1 >= S;
}

/* Still text gliding side to side by its shift byte, easing into each end. Short
 * text is padded to 8 cells: the clock's length, whose 0..16 range the pendulum
 * has shown on the real globe (shift 0 is as far right as text goes). */
static int bounce(const ga_anim_t *a, const ga_input_t *in, const cells_t *t, uint32_t k, out_t *o)
{
    int n = t->n, N = n;
    if (N < 8) N = 8;
    if (N < a->amp) N = a->amp;
    if (N > GA_CELLS_MAX) N = GA_CELLS_MAX;
    int left = (N - n) / 2;
    for (int i = 0; i < left; i++) put_ch(o, ' ');
    put_all(o, t);
    for (int i = left + n; i < N; i++) put_ch(o, ' ');

    int c = in->shift >= 0 ? (in->shift > 255 ? 255 : in->shift) : N;
    int amp = a->amp ? a->amp : N <= 12 ? (36 - 3 * N > 2 ? 36 - 3 * N : 2) : 3 * N - 36;   // fill the ~72-column window
    if (amp > c) amp = c;
    if (amp > 255 - c) amp = 255 - c;
    int lo = c - amp, hi = c + amp;

    int64_t step = a->ms, air = ga_airtime_ms((size_t)N, in->gap_ms);
    if (step < air) step = air;
    if (step < in->min_ms) step = in->min_ms;
    int64_t half = a->period / 2, P = a->pause < half ? a->pause : half - 1, M = half - P;
    int64_t tm = (int64_t)k * step + P + M / 2;                 // start mid-swing, in the centre
    int64_t cyc = tm % (2 * half), u = cyc % half, en = 0, ed = M * M * M;
    if (u >= P) { int64_t x = u - P; en = 3 * M * x * x - 2 * x * x * x; }   // smoothstep
    int64_t d = ((int64_t)(hi - lo) * en * 2 + ed) / (2 * ed);
    return (int)(cyc < half ? lo + d : hi - d);
}

static bool directional(const cells_t *s)
{
    for (int i = 0; i < s->n; i++) if (s->cp[i] == '<' || s->cp[i] == '>') return true;
    return false;
}

/* The text sweeps a field of `width` cells in whole-cell steps; shapes with an
 * arrow in them turn round (=> becomes <=), and a trail can follow. */
static void scanner(const ga_anim_t *a, const cells_t *t, uint32_t k, out_t *o)
{
    cells_t star;
    const cells_t *s = t;
    if (!t->n) { split("*", &star); s = &star; }
    int L = s->n < GA_CELLS_MAX - 1 ? s->n : GA_CELLS_MAX - 1;
    int W = a->width > L + 1 ? a->width : L + 1, P = W - L;
    uint32_t m = k % (uint32_t)(2 * P);
    int pos = m <= (uint32_t)P ? (int)m : 2 * P - (int)m;
    bool right = m < (uint32_t)P, flip = !right && directional(s);
    int what[GA_CELLS_MAX];                                     // -1 background, -2.. trail, 0.. shape cell
    for (int i = 0; i < W; i++) what[i] = -1;
    int tl = (int)strlen(a->trail);
    for (int j = 0; j < tl; j++) {
        int c = right ? pos - 1 - j : pos + L + j;
        if (c >= 0 && c < W) what[c] = -2 - j;
    }
    for (int i = 0; i < L; i++) what[pos + i] = flip ? L - 1 - i : i;
    for (int i = 0; i < W; i++) {
        int w = what[i];
        if (w == -1) put_ch(o, a->bg);
        else if (w <= -2) put_ch(o, a->trail[-2 - w]);
        else if (flip && s->cp[w] == '<') put_ch(o, '>');
        else if (flip && s->cp[w] == '>') put_ch(o, '<');
        else if (flip && s->cp[w] == '(') put_ch(o, ')');
        else if (flip && s->cp[w] == ')') put_ch(o, '(');
        else put_cell(o, s, w);
    }
}

static void sprites(const ga_anim_t *a, const cells_t *t, uint32_t k, out_t *o)
{
    int W = a->width;
    char c[GA_CELLS_MAX];
    switch (a->sprite) {
    case GA_SPRITE_PACMAN: {           // C chomps along a row of dots (or your text), then it all comes back
        if (W < t->n) W = t->n;
        int p = (int)(k % (uint32_t)(W + 3)) - 1;
        for (int i = 0; i < W; i++) {
            if (p >= 0 && i < p) put_ch(o, ' ');
            else if (p >= 0 && i == p) put_ch(o, k % 2 ? 'O' : 'C');
            else if (t->n) { if (i < t->n) put_cell(o, t, i); else put_ch(o, ' '); }
            else put_ch(o, i % 2 ? '.' : ' ');
        }
        return;
    }
    case GA_SPRITE_CHASE: {            // => closes in on a dawdling >, which dashes off; the chaser follows out
        if (W < 6) W = 6;
        int s = W / 3, m = (int)(k % (uint32_t)(W + 3)), H = m - 1;
        int L = m <= 2 * s - 2 ? s + m / 2 : 2 * s - 1 + 2 * (m - (2 * s - 2));
        memset(c, ' ', (size_t)W);
        if (L < W) c[L] = '>';
        if (H >= 0 && H < W) c[H] = '>';
        if (H >= 1 && H <= W) c[H - 1] = '=';
        break;
    }
    case GA_SPRITE_PULSE: {            // . o O o . rippling out from the middle
        static const char lv[8] = { '.', 'o', 'O', 'o', '.', ' ', ' ', ' ' };
        for (int i = 0; i < W; i++) {
            int d = abs(2 * i - (W - 1)) / 2;
            c[i] = lv[(k % 8 + 8 - (uint32_t)d % 8) % 8];
        }
        break;
    }
    default: {                         // a fish swims across, turns, swims back
        if (W < 5) W = 5;
        int P = W - 3, pos;
        uint32_t m = k % (uint32_t)(2 * P + 2);
        bool right = m <= (uint32_t)P;
        pos = right ? (int)m : m == (uint32_t)P + 1 ? P : 2 * P + 1 - (int)m;
        memset(c, ' ', (size_t)W);
        memcpy(c + pos, right ? "><>" : "<><", 3);
        break;
    }
    }
    for (int i = 0; i < W; i++) put_ch(o, c[i]);
}

/* A Wolfram cellular automaton on a ring of `width` cells: one generation per
 * step, starting again every `gens` generations or when it dies out or fills. */
static void automaton(const ga_anim_t *a, const cells_t *t, uint32_t k, out_t *o)
{
    int W = a->width;
    uint32_t b = k / a->gens, g = k % a->gens, seed = text_seed(a, t);
    uint8_t s[GA_CELLS_MAX], nx[GA_CELLS_MAX];
    for (int i = 0; i < W; i++) s[i] = a->start == GA_START_RANDOM ? (uint8_t)(rnd(seed, b, (uint32_t)i) & 1) : i == W / 2;
    for (uint32_t step = 1; step <= g; step++) {
        int alive = 0;
        for (int i = 0; i < W; i++) {
            int idx = s[(i + W - 1) % W] << 2 | s[i] << 1 | s[(i + 1) % W];
            nx[i] = (uint8_t)((a->rule >> idx) & 1);
            alive += nx[i];
        }
        if (alive == 0 || alive == W) {
            for (int i = 0; i < W; i++) nx[i] = (uint8_t)(rnd(seed, b * 256 + step, (uint32_t)i + 1000) & 1);
        }
        memcpy(s, nx, (size_t)W);
    }
    for (int i = 0; i < W; i++) put_ch(o, s[i] ? a->live : a->dead);
}

void ga_step(const ga_anim_t *a, const ga_input_t *in, uint32_t k, ga_step_t *out)
{
    out->text[0] = '\0';
    out->shift = -1;
    out->blink = -1;
    out->done = false;
    out->tmpl = false;
    cells_t t;
    split(in->target, &t);
    out_t o = { out->text, 0, 0 };
    switch (a->type) {
    case GA_DECODE:     out->done = decode(a, &t, k, &o); break;
    case GA_TYPEWRITER: out->done = typewriter(a, &t, k, &o); break;
    case GA_COUNT:      out->done = count(a, in, &t, k, &o); break;
    case GA_BOUNCE:     out->shift = bounce(a, in, &t, k, &o); break;
    case GA_SCANNER:    scanner(a, &t, k, &o); break;
    case GA_SPRITES:    sprites(a, &t, k, &o); break;
    case GA_AUTOMATON:  automaton(a, &t, k, &o); break;
    case GA_SEQUENCE:
        if (a->ntexts && a->texts && (a->loop || k < a->ntexts)) {
            const char *p = a->texts;
            for (uint32_t i = k % a->ntexts; i; i--) p += strlen(p) + 1;
            size_t n = strlen(p);                              // a template: not cut at 35 cells
            memcpy(out->text, p, n < GA_TEXT_BYTES - 1 ? n + 1 : GA_TEXT_BYTES - 1);
            out->text[GA_TEXT_BYTES - 1] = '\0';
            out->tmpl = true;
        } else {
            put_all(&o, &t);
            out->done = true;
        }
        break;
    default:
        put_all(&o, &t);
        out->done = true;
        break;
    }
    cells_t shown;
    split(out->text, &shown);                                  // at most 35: what the frame will carry
    uint32_t ms = a->ms, air = ga_airtime_ms((size_t)shown.n, in->gap_ms);
    if (ms < air) ms = air;
    if (ms < in->min_ms) ms = in->min_ms;
    out->ms = ms;
}
