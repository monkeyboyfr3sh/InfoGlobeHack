#pragma once

/* Custom animations (docs/animations.md): motion the globe has no effect for,
 * made by sending a sequence of still frames (command 05) one step at a time.
 *
 * Pure C, no FreeRTOS or ESP-IDF, so it builds on a PC (hello_world/test_host)
 * and is mirrored exactly by web/src/lib/anim.js: the same animation, input
 * and step number give the same text, position and step time on both sides,
 * including the pseudo-random parts (a stateless hash seeded from the inputs).
 *
 * Text is UTF-8; one character (code point) is one cell, one glyph on the
 * globe. Every step is a pure function of its number, so the engine can skip
 * steps when it falls behind and the preview can jump to any moment. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GA_CELLS_MAX     35                      // = GLOBE_TEXT_MAX, longest text in a frame
#define GA_TEXT_BYTES    (GA_CELLS_MAX * 4 + 1)  // a step's text (UTF-8) plus NUL
#define GA_SEQ_MAX       16                      // sequence: most texts
#define GA_SEQ_TEXT_MAX  63                      // sequence: longest text in bytes (before live values)
#define GA_FRAME_EXTRA   5                       // bytes of a 05 frame besides its text
#define GA_MS_MIN        20
#define GA_MS_MAX        60000

typedef enum {
    GA_NONE = 0,
    GA_DECODE,          // finite: random glyphs lock into the text
    GA_TYPEWRITER,      // finite: one character at a time
    GA_COUNT,           // finite: numbers roll to their value
    GA_BOUNCE,          // loops: still text glides side to side (shift byte)
    GA_SCANNER,         // loops: the text sweeps a field in whole cells
    GA_SPRITES,         // loops: little cartoons
    GA_AUTOMATON,       // loops: 1-D cellular automaton
    GA_SEQUENCE,        // your own texts, looping or once
    GA_FLAP,            // finite: every letter flips through its alphabet to the text (departure board)
    GA_SPARKLE,         // loops: stars flash over the text here and there
    GA_WAVE,            // loops: a ripple of swapped capitals runs along the text
    GA_TYPES
} ga_type_t;

enum { GA_ORDER_LR = 0, GA_ORDER_RL, GA_ORDER_RANDOM };
enum { GA_NOISE_MATCH = 0, GA_NOISE_SYMBOLS, GA_NOISE_MIXED };
enum { GA_COUNT_NUMBER = 0, GA_COUNT_DIGITS };
enum { GA_SPRITE_PACMAN = 0, GA_SPRITE_CHASE, GA_SPRITE_PULSE, GA_SPRITE_FISH };
enum { GA_START_SINGLE = 0, GA_START_RANDOM };

/* An animation and its options (set with ga_init, then ga_set_num / ga_set_str,
 * which validate and clamp exactly as anim.js normalizeAnim() does). */
typedef struct {
    uint8_t     type;       // ga_type_t
    uint16_t    ms;         // time per step; the engine never goes below the frame's airtime
    uint8_t     lead;       // decode: steps of pure noise before the first letter locks; wave: calm steps between ripples
    uint8_t     order;      // decode, flap: GA_ORDER_*; wave: lr or rl
    uint8_t     noise;      // decode: GA_NOISE_*
    uint8_t     steps;      // count: steps to reach the value; flap: most flips a letter makes
    uint8_t     density;    // sparkle: how many stars, 1-20 (of 40 chances per cell and step)
    uint8_t     crest;      // wave: letters under the ripple
    uint8_t     mode;       // count: GA_COUNT_*
    uint16_t    period;     // bounce: ms there and back
    uint16_t    pause;      // bounce: ms resting at each end
    uint8_t     amp;        // bounce: dot columns either side of the centre, 0 = auto
    uint8_t     width;      // scanner, sprites, automaton: cells
    uint8_t     sprite;     // sprites: GA_SPRITE_*
    uint8_t     rule;       // automaton: Wolfram rule number
    uint8_t     gens;       // automaton: generations before it starts again
    uint8_t     start;      // automaton: GA_START_*
    bool        loop;       // sequence: loop, or play once and then show the frame's text
    char        cursor;     // typewriter: cursor glyph, 0 = none
    char        bg;         // scanner: background glyph
    char        live, dead; // automaton: glyphs for live and dead cells
    char        trail[4];   // scanner: glyphs behind the moving text, nearest first
    uint32_t    seed;       // mixed into the random numbers
    const char *texts;      // sequence: `ntexts` NUL-terminated templates back to back
    uint8_t     ntexts;
} ga_anim_t;

typedef struct {
    const char *target;     // the frame's text, live values filled in
    const char *from;       // count, flap: the text before a change; NULL or "" = from nothing
    int         shift;      // the frame's own position, -1 = centred (shift = length)
    uint16_t    gap_ms;     // IR idle gap after every frame
    uint16_t    min_ms;     // extra floor for every step, 0 = airtime only
} ga_input_t;

typedef struct {
    char     text[GA_TEXT_BYTES];
    int      shift;         // position for this step, -1 = the frame's own
    int      blink;         // blink byte characters, -1 = the frame's own
    uint32_t ms;            // until the next step: max(option, airtime, min_ms)
    bool     done;          // finite animation: this step shows the final text
    bool     tmpl;          // `text` is a template (sequence): fill in live values first
} ga_step_t;

/* Defaults for a type by name ("decode", ...); false (and GA_NONE) if unknown. */
bool        ga_init(ga_anim_t *a, const char *type);
const char *ga_type_name(int type);
/* Options by JSON name; unknown names and invalid values are ignored. */
void        ga_set_num(ga_anim_t *a, const char *key, double v);
void        ga_set_str(ga_anim_t *a, const char *key, const char *v);
/* Append a sequence text to `pool` (which must directly follow the texts
 * already added for this animation, or be the start when there are none).
 * Texts are clipped to GA_SEQ_TEXT_MAX bytes on a character boundary.
 * Returns the bytes used, 0 if it doesn't fit or there are GA_SEQ_MAX already. */
size_t      ga_add_text(ga_anim_t *a, char *pool, size_t room, const char *text);

/* Ends on the full text (plays as an entrance) rather than looping. */
bool        ga_finite(const ga_anim_t *a);
/* Time on the IR link for a still frame of `cells` characters: 1 ms per bit plus the gap. */
uint32_t    ga_airtime_ms(size_t cells, unsigned gap_ms);
/* What to show at step k (0 = first). */
void        ga_step(const ga_anim_t *a, const ga_input_t *in, uint32_t k, ga_step_t *out);
