#pragma once

/* Frame compiler: turns a designed frame (text + entrance + style) into
 * InfoGlobe protocol bytes. Nothing above this layer needs to know the
 * command bytes; see docs/protocol.md for the formats it emits. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GLOBE_TEXT_MAX   35      // longest text the globe shows in one frame
#define GLOBE_FRAME_MAX  40      // longest compiled frame (= MAX_MESSAGE_LEN)

typedef enum {
    GLOBE_AFTER_SPIN = 0,   // text circles the band (default after any entrance)
    GLOBE_AFTER_HOLD,       // still text at `shift`, optional blinking start
    GLOBE_AFTER_FLASH,      // whole text blinks on/off (~1 Hz): 05 frame, blink = len + 1
    GLOBE_AFTER_INSTANT,    // still, left-aligned, appears with no transition
} globe_after_t;

typedef struct {
    int           entrance;     // effect 0..37, or -1 for none
    globe_after_t after;
    int           blink_chars;  // HOLD: number of leading characters that blink (0 = none)
    int           shift;        // HOLD: horizontal position, -1 = centred (text length)
} globe_style_t;

typedef struct {
    int         id;
    const char *name;
    const char *group;
    int         duration_ms;    // approximate length of the entrance animation
    int         same_as;        // -1, or the effect it looks identical to
} globe_effect_info_t;

extern const globe_effect_info_t g_globe_effects[];
extern const int                 g_globe_effect_count;

/* Find an effect by number ("12") or by name, case-insensitive ("pincer"). -1 if unknown. */
int globe_effect_lookup(const char *name_or_number);

/* UTF-8 text -> globe character codes (accented letters map to their control
 * codes, the envelope icon to 0x80). Characters the font lacks become spaces
 * and are counted in *unsupported (may be NULL). Returns the encoded length,
 * truncated to `cap`. */
size_t globe_text_encode(const char *utf8, uint8_t *out, size_t cap, int *unsupported);

/* Compile one frame (the text must already be encoded). Writes up to two
 * frames: `first` is sent now; `second` (len > 0) is sent once the entrance
 * has played, for entrance + hold/flash. Returns false on bad input. */
bool globe_frame_compile(const uint8_t *text, size_t text_len, const globe_style_t *style,
                         uint8_t *first, size_t *first_len,
                         uint8_t *second, size_t *second_len);

/* The single-byte "clear screen" frame. */
#define GLOBE_CMD_CLEAR 0x04
