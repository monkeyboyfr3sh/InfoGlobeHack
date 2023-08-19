#include "frame_maker_task.h"

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
#include "tcp_server_task.h"

static const char * TAG = "FR_MKR";

void run_boot_animation(QueueHandle_t *buffer_queue)
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
    
    send_string_to_queue(*buffer_queue, msg, bw+5);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

void frame_maker_task(void *pvParameters)
{
    // Grab queue
    QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);
    
    // Set boot animation
    run_boot_animation(&display_queue);
    
    // Create TCP server thread
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)display_queue, 5, NULL);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}