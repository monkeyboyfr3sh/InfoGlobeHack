#pragma once

/* Feeds: background tasks that fetch data from the internet and publish it
 * through value_store, so scenes can show {temp}, {plane}, ...
 * Each feed lives in its own file and starts its own task. Feeds read their
 * options from settings_load() (scene_engine.h); the web app saves them with
 * POST /api/v1/settings, which merges any keys. */

#include <stdint.h>

void feeds_start(void);            // called once, after the clock has synced

void feed_weather_start(void);     // feed_weather.c
void feed_aircraft_start(void);    // feed_aircraft.c

/* Goes up by one every time the settings are saved. A feed keeps the number
 * it last saw and, when it differs, re-reads its settings and fetches again
 * straight away (check it about once a second while waiting for the next poll). */
uint32_t feeds_settings_generation(void);
void     feeds_settings_changed(void);     // called by POST /api/v1/settings
