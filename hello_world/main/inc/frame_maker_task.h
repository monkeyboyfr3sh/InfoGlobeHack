#pragma once

#include <stdbool.h>

extern volatile bool g_time_on;

void frame_maker_task(void *pvParameters);