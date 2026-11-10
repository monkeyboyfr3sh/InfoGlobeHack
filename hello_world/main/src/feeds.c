#include "feeds.h"

void feeds_start(void)
{
    feed_weather_start();
    feed_aircraft_start();
}
