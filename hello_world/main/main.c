#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include "message_type.h"
#include "ir_led_task.h"
#include "frame_maker_task.h"

static const char * TAG = "MAIN";

void app_main(void)
{
    // Init buffer queue
    QueueHandle_t display_queue = xQueueCreate(10, sizeof(msg_buffer_t));
    if (display_queue == NULL) {
        printf("Failed to create queue\n");
        return;
    }

    // Start ir led task
    xTaskCreate(ir_tx_task, "ir_tx", 4096, (void*)display_queue, 20, NULL);

    // Start frame maker task
    xTaskCreate(frame_maker_task, "frame_maker", 4096, (void*)display_queue, 20, NULL);
}