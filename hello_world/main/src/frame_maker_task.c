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
#include "network.h"
#include "scene_engine.h"
#include "feeds.h"
#include "auth.h"

/* How long a new IP address takes over the globe from the clock. */
#define IP_SHOW_MS          20000
#define IP_POLL_MS          1000
/* Holding the button this long clears the web access token (lost-token recovery). */
#define TOKEN_RESET_HOLD_MS 10000

#if CONFIG_INFOGLOBE_ETHERNET && (CONFIG_INFOGLOBE_BUTTON_GPIO == 0)
#error "GPIO0 is the Ethernet RMII clock input; pick another button GPIO in menuconfig (e.g. 32)"
#endif

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

    // Weather, aircraft, ...: background fetchers that publish live values
    feeds_start();

    // OTA and button commands are now handled by the HTTP server
    
    // TickType_t ani_1_tick = xTaskGetTickCount();   // for the disabled animations below
    // TickType_t ani_2_tick = xTaskGetTickCount();

    bool prev_io_level = true;
    bool curr_io_level = true;
    TickType_t ip_poll_tick = xTaskGetTickCount();
    TickType_t ip_show_until = 0;
    bool showing_ip = false;
    TickType_t press_tick = 0;
    bool token_reset_armed = false;
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
            press_tick = xTaskGetTickCount();
            token_reset_armed = true;

            if(g_time_on){
                ESP_LOGI(TAG,"Time display is now enabled!");
            } else {
                ESP_LOGI(TAG,"Time display is now disabled!");
            }
        }
        // Still held after 10 s: clear the access token and say so on the
        // globe (manual mode; a short press goes back to the clock).
        if (curr_io_level == 0 && token_reset_armed &&
            (xTaskGetTickCount() - press_tick) >= pdMS_TO_TICKS(TOKEN_RESET_HOLD_MS)) {
            token_reset_armed = false;
            auth_clear();
            ESP_LOGW(TAG, "Button held 10 s: web access token cleared");
            const char *msg = "TOKEN CLEARED";
            g_time_on = false;
            scene_engine_note_manual(msg);
            string_with_blink_shift(display_queue, msg, strlen(msg), 0, strlen(msg));
        }
        prev_io_level = curr_io_level;

        // Announce a new address when the preferred interface changes
        // (cable pulled -> Wi-Fi IP, cable back -> Ethernet IP, both gone).
        TickType_t now = xTaskGetTickCount();
        if ((now - ip_poll_tick) >= pdMS_TO_TICKS(IP_POLL_MS)) {
            ip_poll_tick = now;
            char ip[16];
            const char *iface = NULL;
            if (network_ip_changed(ip, sizeof(ip), &iface)) {
                if (ip[0]) {
                    ESP_LOGI(TAG, "Preferred address is now %s (%s)", ip, iface);
                } else {
                    ESP_LOGW(TAG, "No network address on any interface");
                }
                // Only take over the globe from the clock, never from text
                // someone put up through the web UI (time mode off).
                if (g_time_on) {
                    if (ip[0]) {
                        single_string_rotate(display_queue, ip, strlen(ip), 1);
                    } else {
                        const char *msg = "No network...";
                        send_string_w_bytes_to_queue(display_queue, msg, strlen(msg), 0x00, 0x00);
                    }
                    ip_show_until = now + pdMS_TO_TICKS(IP_SHOW_MS);
                    showing_ip = true;
                }
            }
        }
        if (showing_ip && (int32_t)(now - ip_show_until) >= 0) {
            showing_ip = false;   // clock redraws within 500 ms (colon blink)
        }

        // The playlist (by default the built-in Clock scene) owns the
        // display unless someone put text up or a new IP is being shown.
        scene_engine_tick(display_queue, g_time_on && !showing_ip);
        
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