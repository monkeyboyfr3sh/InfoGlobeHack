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

/* Something outside the engine put this text up (Designer, classic page). */
void scene_engine_note_manual(const char *text);

/* {"mode":"playlist|scene|manual","scene":..,"name":..,"frame":n,"text":..} */
cJSON *scene_engine_status(void);

/* ---- storage (scene_store) ---- */
bool   scene_id_valid(const char *id);
cJSON *scene_load(const char *id);                 // NULL if missing (clock is built in)
bool   scene_save(const char *id, const cJSON *scene);
bool   scene_delete(const char *id);
cJSON *scene_list(void);                           // [{id,name,frames}]
cJSON *playlist_load(void);                        // never NULL
bool   playlist_save(const cJSON *playlist);
void   scene_engine_reload(void);                  // storage changed: re-read at next tick
