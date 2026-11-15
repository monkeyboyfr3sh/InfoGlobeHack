#pragma once

/* Scenes and the playlist (Phase 2).
 *
 * A scene is a list of frames (text with live values, entrance, style,
 * duration); the playlist is a list of {scene, seconds}. Both are JSON files
 * on the "notes" partition (sc_<id>.json, playlist.json), so they survive
 * reflashing. The engine runs inside the display loop (frame_maker_task)
 * whenever the globe is in "playlist" mode (g_time_on); the built-in "clock"
 * scene is the default playlist.
 */

#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cJSON.h"

#define SCENE_ID_MAX      24
#define SCENE_MAX_FRAMES  24
#define PLAYLIST_MAX      24

/* Called every ~10 ms by the display loop; `active` is false while something
 * else owns the display (manual text, IP announcement, IR test). */
void scene_engine_tick(QueueHandle_t queue, bool active);

/* Replace live values ({time}, {date}, {clock}, ...) in `tmpl`.
 * *live is set when the result changes over time. */
void scene_subst(const char *tmpl, char *out, size_t cap, bool *live);

/* Play a scene now (over the playlist) for `seconds` (0 = one pass through
 * its frames), then return to the playlist. Returns false if it doesn't exist. */
bool scene_engine_play(const char *id, unsigned seconds);

/* The kitchen timer: count down `seconds` on the globe (with `label` in front,
 * may be NULL), then flash TIME'S UP for 15 seconds. 0 cancels a running one, or
 * dismisses the time's up. The seconds left are in the status as "timer". */
bool     scene_engine_timer(unsigned seconds, const char *label);
unsigned scene_engine_timer_left(void);

/* Skip to the next playlist item (ends a "play now" scene). */
void scene_engine_next(void);

/* Back to the playlist now (also ends a "play now" scene). */
void scene_engine_resume(void);

/* Something outside the engine put this text up (Designer, classic page). */
void scene_engine_note_manual(const char *text);

/* {"mode":"playlist|scene|manual|off","scene":..,"name":..,"frame":n,"text":..,"timer":seconds left}
 * "off": the display is switched off (settings "display": false). */
cJSON *scene_engine_status(void);

/* ---- storage (scene_store) ---- */
bool   scene_id_valid(const char *id);
cJSON *scene_load(const char *id);                 // NULL if missing (clock is built in)
bool   scene_save(const char *id, const cJSON *scene);
bool   scene_delete(const char *id);
cJSON *scene_list(void);                           // [{id,name,frames}]
cJSON *playlist_load(void);                        // never NULL
bool   playlist_save(const cJSON *playlist);
void   scene_engine_reload(void);                  // re-read everything and start again at next tick
void   scene_engine_refresh(void);                 // playlist or settings changed: re-read, keep playing if the current scene is still listed
cJSON *settings_load(void);                        // {"lat":..,"lon":..,"display":true,"displayOffAt":"23:00","displayOnAt":"07:00"}
bool   settings_save(const cJSON *patch);          // merge and apply
void   settings_boot(void);                        // apply the saved settings once the notes partition is readable
