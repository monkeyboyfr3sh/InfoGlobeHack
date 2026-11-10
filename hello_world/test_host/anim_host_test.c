/* Host check that globe_anim.c (firmware) and web/src/lib/anim.js (designer
 * preview) agree: replays anim_vectors.txt, which anim.js produced, through
 * the C module and compares every field of every step. See run.sh. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globe_anim.h"

static size_t unhex(const char *h, char *out, size_t cap)
{
    size_t n = 0;
    if (*h == 'x') h++;
    while (h[0] && h[1] && n + 1 < cap) {
        unsigned b;
        if (sscanf(h, "%2x", &b) != 1) break;
        out[n++] = (char)b;
        h += 2;
    }
    out[n] = '\0';
    return n;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "anim_vectors.txt";
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }

    static char line[8192], pool[4096], target[512], from[512], want[512], tok[512];
    ga_anim_t a;
    ga_input_t in = { 0 };
    char type[32] = "";
    long steps = 0, cases = 0, fails = 0, lineno = 0;
    bool have_from = false;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == 'A') {
            char *save = NULL, *t = strtok_r(line + 2, " ", &save);
            snprintf(type, sizeof(type), "%s", t ? t : "");
            ga_init(&a, type);
            size_t used = 0;
            while ((t = strtok_r(NULL, " ", &save)) != NULL) {
                char *eq = strchr(t, '=');
                if (!eq || !eq[1] || eq[2] != ':') continue;
                *eq = '\0';
                char kind = eq[1], *val = eq + 3;
                if (kind == 'n') ga_set_num(&a, t, strtod(val, NULL));
                else if (kind == 'b') ga_set_num(&a, t, atoi(val));
                else if (kind == 's') { unhex(val, tok, sizeof(tok)); ga_set_str(&a, t, tok); }
                else if (kind == 't') {
                    for (char *s2 = NULL, *h = strtok_r(val, ",", &s2); h; h = strtok_r(NULL, ",", &s2)) {
                        unhex(h, tok, sizeof(tok));
                        used += ga_add_text(&a, pool + used, sizeof(pool) - used, tok);
                    }
                }
            }
            cases++;
        } else if (line[0] == 'I') {
            char th[1100], fh[1100];
            int shift, gap, mn;
            if (sscanf(line, "I %1099s %1099s %d %d %d", th, fh, &shift, &gap, &mn) != 5) { fprintf(stderr, "bad line %ld\n", lineno); return 2; }
            unhex(th, target, sizeof(target));
            have_from = strcmp(fh, "-") != 0;
            if (have_from) unhex(fh, from, sizeof(from));
            in.target = target;
            in.from = have_from ? from : NULL;
            in.shift = shift;
            in.gap_ms = (uint16_t)gap;
            in.min_ms = (uint16_t)mn;
        } else if (line[0] == 'S') {
            unsigned long k, ms;
            char wh[1100];
            int shift, blink, done, tmpl;
            if (sscanf(line, "S %lu %1099s %d %d %lu %d %d", &k, wh, &shift, &blink, &ms, &done, &tmpl) != 7) { fprintf(stderr, "bad line %ld\n", lineno); return 2; }
            unhex(wh, want, sizeof(want));
            ga_step_t s;
            ga_step(&a, &in, (uint32_t)k, &s);
            steps++;
            if (strcmp(s.text, want) || s.shift != shift || s.blink != blink || s.ms != ms || s.done != (done != 0) || s.tmpl != (tmpl != 0)) {
                if (fails++ < 20) {
                    fprintf(stderr, "line %ld: %s target \"%s\" k=%lu\n  JS: \"%s\" shift %d blink %d ms %lu done %d tmpl %d\n"
                            "   C: \"%s\" shift %d blink %d ms %lu done %d tmpl %d\n",
                            lineno, type, target, k, want, shift, blink, ms, done, tmpl,
                            s.text, s.shift, s.blink, (unsigned long)s.ms, s.done, s.tmpl);
                }
            }
        }
    }
    fclose(f);
    printf("%ld animations, %ld steps compared, %ld mismatches\n", cases, steps, fails);
    return fails ? 1 : 0;
}
