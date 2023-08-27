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
#include "ota_server_task.h"

static const char * TAG = "FR_MKR";

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

    // Create OTA server thread
    xTaskCreate(ota_server_task, "ota_server_task", 4096, (void*)display_queue, 5, NULL);
    
    TickType_t ani_1_tick = xTaskGetTickCount();
    TickType_t ani_2_tick = xTaskGetTickCount();

    bool prev_io_level = true;
    bool curr_io_level = true;
    gpio_set_direction(GPIO_NUM_0,GPIO_MODE_INPUT);
    bool time_on = false;
    
    while(1)
    {

        // Detect falling edge
        curr_io_level = gpio_get_level(GPIO_NUM_0);
        if( (curr_io_level==0) && (prev_io_level==true) ){
            time_on = !time_on;
            
            if(time_on){
                ESP_LOGI(TAG,"Time display is now enabled!");
            } else {
                ESP_LOGI(TAG,"Time display is now disabled!");
            }
        }
        prev_io_level = curr_io_level;

        if( time_on ) {
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