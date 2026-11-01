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
#include "driver/gpio.h"

#include "message_type.h"
#include "sntp_helper.h"
#include "infoglobe_animations.h"

#include "tcp_server_task.h"

static const char * TAG = "FR_MKR";

volatile bool g_time_on = true;

void frame_maker_task(void *pvParameters)
{
    // Grab queue
    QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);
    
    // Set boot animation
    run_boot_animation(display_queue, 4000);

    // Init time sync
    init_time_sync();

    // Create TCP server thread
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)display_queue, 5, NULL);
    
    // Once time sync completes, can start
    wait_for_time_sync();

    // OTA and button commands are now handled by the HTTP server
    
    TickType_t ani_1_tick = xTaskGetTickCount();
    TickType_t ani_2_tick = xTaskGetTickCount();

    bool prev_io_level = true;
    bool curr_io_level = true;
    const gpio_config_t button_conf = {
        .pin_bit_mask = 1ULL << CONFIG_INFOGLOBE_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   // no-op on GPIO34-39 (input only)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_conf));

    while(1)
    {

        // Detect falling edge (physical button still works)
        curr_io_level = gpio_get_level(CONFIG_INFOGLOBE_BUTTON_GPIO);
        if( (curr_io_level==0) && (prev_io_level==true) ){
            g_time_on = !g_time_on;

            if(g_time_on){
                ESP_LOGI(TAG,"Time display is now enabled!");
            } else {
                ESP_LOGI(TAG,"Time display is now disabled!");
            }
        }
        prev_io_level = curr_io_level;

        if( g_time_on ) {
            // Always run time animation
            run_animation_time_1(display_queue);
        }
        
        // // Occasionally run animation 1
        // if ( (xTaskGetTickCount()-ani_1_tick) > pdMS_TO_TICKS(100000) ) {
        //     ani_1_tick = xTaskGetTickCount();
        //     run_world_domination(display_queue);
        // }

        // // Occasionally run animation 2
        // if ( (xTaskGetTickCount()-ani_2_tick) > pdMS_TO_TICKS(60000) ) {
        //     ani_2_tick = xTaskGetTickCount();
        //     run_hello_world(display_queue);
        // }

        vTaskDelay(1);
    }

    // Exit
    vTaskDelete(NULL);
}