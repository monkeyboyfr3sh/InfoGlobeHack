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


void run_animation_time_1(QueueHandle_t display_queue)
{
    const size_t msg_max = 32;
    char msg[msg_max];
    
    // Zero memory
    memset(msg,0,msg_max);

    // Now animate!
    size_t bw = 0x0C;
    msg[1] = 0x00;
    bw = snprintf(&msg[2], msg_max, "12:00am");
    msg[bw+4] = 0x01;
    
    send_string_to_queue(display_queue, msg, bw+5);
    vTaskDelay(pdMS_TO_TICKS(6000));
}
