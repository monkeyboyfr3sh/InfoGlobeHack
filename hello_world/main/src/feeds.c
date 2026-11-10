#include "feeds.h"

static volatile uint32_t s_generation;

uint32_t feeds_settings_generation(void) { return s_generation; }
void     feeds_settings_changed(void)    { s_generation++; }

void feeds_start(void)
{
    feed_weather_start();
    feed_aircraft_start();
}
