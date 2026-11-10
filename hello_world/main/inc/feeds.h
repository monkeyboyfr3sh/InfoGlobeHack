#pragma once

/* Feeds: background tasks that fetch data from the internet and publish it
 * through value_store, so scenes can show {temp}, {plane}, ...
 * Each feed lives in its own file and starts its own task. */

void feeds_start(void);            // called once, after the clock has synced

void feed_weather_start(void);     // feed_weather.c
void feed_aircraft_start(void);    // feed_aircraft.c
