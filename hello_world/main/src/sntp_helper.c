#include "sntp_helper.h"
#include <time.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"


#define TIME_SYNC_BIT (1 << 0) // Define a bit for time synchronization event
EventGroupHandle_t timeSyncEventGroup = NULL; // Event group handle

void init_time_sync(void)
{
    timeSyncEventGroup = xEventGroupCreate();
    // Local time incl. daylight-saving rules (menuconfig: InfoGlobe -> Time zone)
    set_time_zone(CONFIG_INFOGLOBE_TIMEZONE);
}

void init_sntp(void) {

    // Create an event group
    if(timeSyncEventGroup == NULL){
        init_time_sync();
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Wait for status to change
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
        vTaskDelay(1);
    }

    // Set bit for others
    xEventGroupSetBits(timeSyncEventGroup, TIME_SYNC_BIT);
}

// Example function to wait for time synchronization
void wait_for_time_sync(void)
{
    // Can't work here
    if (timeSyncEventGroup==NULL){
        return;
    }

    // Wait for the time synchronization bit to be set
    EventBits_t bits = xEventGroupWaitBits(timeSyncEventGroup, TIME_SYNC_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    if (bits & TIME_SYNC_BIT) {
        // Time synchronization has occurred, continue with your code
    }
}
void get_current_time(struct tm *timeinfo)
{
    time_t now;
    time(&now);
    // TZ is set in init_time_sync(); localtime applies the offset and DST.
    // (Previously a fixed -5 h offset, which was only right during CDT.)
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
