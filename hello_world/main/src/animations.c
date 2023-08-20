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

void string_with_blink_shift(QueueHandle_t display_queue, const char * str_buff, size_t str_len, uint8_t blink_idx, uint8_t shift_idx)
{
    ESP_LOGI(TAG,"string length: %d",str_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG,str_buff,str_len,ESP_LOG_INFO);

    char msg[str_len+5];
    memset(msg,0,sizeof(msg));

    // Populate message
    msg[0] = 0x05;
    msg[1] = 0x00;
    memcpy(&msg[2], str_buff, str_len);
    msg[str_len + 2] = 0x00;
    msg[str_len + 3] = blink_idx;
    msg[str_len + 4] = shift_idx;

    // Send it to display
    send_string_to_queue(display_queue, msg, str_len + 5);
}

void run_boot_animation(QueueHandle_t display_queue, uint32_t animation_dur_ms)
{
    // Now animate!
    const char *boot_msg = "< Booting >";
    string_with_blink_shift(display_queue,
        boot_msg, strlen(boot_msg),
        0x0C, strlen(boot_msg)-1);
    vTaskDelay(pdMS_TO_TICKS(animation_dur_ms));
}

void run_animation_1(QueueHandle_t display_queue, uint32_t animation_dur_ms)
{
    const char *animation_msg = "H E L L O";
    string_with_blink_shift(display_queue,
        animation_msg, strlen(animation_msg),
        strlen(animation_msg)+2, strlen(animation_msg)-1);
    vTaskDelay(pdMS_TO_TICKS(animation_dur_ms));
}

void run_animation_2(QueueHandle_t display_queue, uint32_t animation_dur_ms)
{
    const char *animation_msg = "W O R L D";
    string_with_blink_shift(display_queue,
        animation_msg, strlen(animation_msg),
        strlen(animation_msg)+2, strlen(animation_msg)-1);
    vTaskDelay(pdMS_TO_TICKS(animation_dur_ms));
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
        string_with_blink_shift(display_queue,
            formatted_time, 8,
            0, 8);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
