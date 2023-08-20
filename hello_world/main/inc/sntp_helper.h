#pragma once

#include <time.h>
#include <stdbool.h>

void init_sntp(void);
void get_current_time(struct tm *timeinfo);
void set_time_zone(const char *tz);
void format_time(char *formatted_time, const struct tm *timeinfo, bool blank_colon);