#include "sntp_helper.h"
#include <time.h>
#include <sntp.h>

void init_sntp(void)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    set_time_zone("America/Chicago"); // Set your desired time zone here

    // Wait for sync to finish
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
        vTaskDelay( pdMS_TO_TICKS(1000) ); // Wait for synchronization
    }
}

void get_current_time(struct tm *timeinfo)
{
    time_t now;
    time(&now);
    int central_time_offset = -5 * 3600; // Central Time (UTC-6) offset in seconds
    now += central_time_offset; // Apply time offset
    localtime_r(&now, timeinfo);
}

void set_time_zone(const char *tz)
{
    setenv("TZ", tz, 1);
    tzset();
}

void format_time(char *formatted_time, const struct tm *timeinfo, bool blank_colon) 
{
    strftime(formatted_time, 12, "%I:%M %p", timeinfo); // Format as HH:MM AM/PM
    if(blank_colon){
        formatted_time[2] = ' ';
    }
}
