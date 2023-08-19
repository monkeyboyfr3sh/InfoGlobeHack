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
#include "animations.h"

static const char * TAG = "FR_MKR";

void frame_maker_task(void *pvParameters)
{
    // Grab queue
    QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);
    
    // Set boot animation
    run_boot_animation(display_queue);
    
    // Create TCP server thread
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)display_queue, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(6000));
    
    while(1)
    {
        run_animation_time_1(display_queue);
        // run_animation_1(display_queue);
        // run_animation_2(display_queue);
        vTaskDelay(1);
    }
}