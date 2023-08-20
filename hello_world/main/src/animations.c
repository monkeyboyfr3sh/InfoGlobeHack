#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include <sys/param.h>
#include "esp_system.h"

#include "message_type.h"
#include "sntp_helper.h"

static const char * TAG = "ANIMATIONS";

void run_boot_animation(QueueHandle_t display_queue)
{
    const size_t msg_max = 32;
    char msg[msg_max];
    
    // Zero memory
    memset(msg,0,msg_max);

    // Now animate!
    size_t bw = 0x0C;
    msg[0] = 0x05;
    msg[1] = 0x00;
    bw = snprintf(&msg[2], msg_max, "< Booting >");
    msg[bw+2] = 0x00;
    msg[bw+3] = 0x0C;
    msg[bw+4] = 0x0D;
    
    send_string_to_queue(display_queue, msg, bw+5);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

void run_animation_1(QueueHandle_t display_queue)
{
    const size_t msg_max = 32;
    char msg[msg_max];
    
    // Zero memory
    memset(msg,0,msg_max);

    // Now animate!
    size_t bw = 0x0C;
    msg[0] = 0x05;
    msg[1] = 0x00;
    bw = snprintf(&msg[2], msg_max, "< Animation 1 >");
    msg[bw+2] = 0x00;
    msg[bw+3] = 0x0C;
    msg[bw+4] = 0x0D;
    
    send_string_to_queue(display_queue, msg, bw+5);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

void run_animation_2(QueueHandle_t display_queue)
{
    const size_t msg_max = 32;
    char msg[msg_max];
    
    // Zero memory
    memset(msg,0,msg_max);

    // Now animate!
    size_t bw = 0x0C;
    msg[0] = 0x05;
    msg[1] = 0x00;
    bw = snprintf(&msg[2], msg_max, "! Animation 2 !");
    msg[bw+2] = 0x00;
    msg[bw+3] = 0x0C;
    msg[bw+4] = 0x0D;
    
    send_string_to_queue(display_queue, msg, bw+5);
    vTaskDelay(pdMS_TO_TICKS(2000));
}


// Define a global variable to store the last minute
static int last_minute = -1;
static int blank_second = -1;
static TickType_t blank_tick = 0;

void run_animation_time_1(QueueHandle_t display_queue)
{
    const size_t msg_max = 32;
    char msg[msg_max];
    static bool blank_colon = false;
    bool blank_draw = false;

    // Zero memory
    memset(msg, 0, msg_max);

    // Get current time
    struct tm timeinfo;
    get_current_time(&timeinfo);

    // Get the current minute
    int current_minute = timeinfo.tm_min;
    // Get the current second
    int current_second = timeinfo.tm_sec;

    // Colon is currently not blanked
    if( blank_colon==false ) {
        if( (current_second % 3) == 0  && (current_second!=blank_second)) {
            blank_second = current_second;
            blank_tick = xTaskGetTickCount();
            blank_colon = true;
            blank_draw = true;
        }
    } 

    // Colon is currently blanked
    else {
        if ( (xTaskGetTickCount() - blank_tick) >= pdMS_TO_TICKS(300) ) {
            blank_colon = false;
            blank_draw = true;
        }
    }


    // Check if the minute has changed
    if (    (current_minute != last_minute) ||
            (blank_draw)
    ) {
        // Update last minute
        last_minute = current_minute;

        // Format time
        char formatted_time[12]; // HH:MM:SS AM/PM format
        format_time(formatted_time, &timeinfo, blank_colon);

        // Print the time
        ESP_LOGI(TAG, "Current time: %s", formatted_time);

        // Now animate!
        size_t bw = 0x0C;
        msg[0] = 0x05;
        msg[1] = 0x00;
        bw = snprintf(&msg[2], msg_max, formatted_time);
        msg[bw + 2] = 0x00;
        msg[bw + 3] = 0x00;
        msg[bw + 4] = 8;

        send_string_to_queue(display_queue, msg, bw + 5);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
