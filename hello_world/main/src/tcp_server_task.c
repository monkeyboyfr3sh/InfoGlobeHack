#include"tcp_server_task.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "example_common_private.h"
#include "string.h"

#include "infoglobe_animations.h"
#include "message_type.h"
#include "sntp_helper.h"
#include "tcp_helpers.h"
#include "http_server_task.h"

static const char * TAG = "TCP_TASK";

void tcp_server_task(void *pvParameters)
{
    // Grab queue
    QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);

    // Set display to the IP addresss
    const size_t msg_max = 32;
    char connect_msg[msg_max];
    size_t bw = snprintf(connect_msg,msg_max,"Connecting...");
    send_string_w_bytes_to_queue(display_queue, connect_msg, bw, 0x00, 0x0);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition invalid, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    // ESP_ERROR_CHECK(wifi_connect());
    esp_err_t wifi_err;
    do {
        wifi_err = example_connect();
        if (wifi_err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi connect failed (%s), retrying...", esp_err_to_name(wifi_err));
            size_t bw2 = snprintf(connect_msg, msg_max, "No WiFi...");
            send_string_w_bytes_to_queue(display_queue, connect_msg, bw2, 0x00, 0x0);
            vTaskDelay(pdMS_TO_TICKS(3000));
            /* A failed example_connect() has already unregistered its event
             * handlers and never registered its shutdown hook, so
             * example_disconnect() would abort on ESP_ERR_NOT_FOUND. Just stop
             * Wi-Fi and destroy the netif so the next attempt starts clean. */
            example_wifi_stop();
        }
    } while (wifi_err != ESP_OK);

    // Set display to the IP addresss
    char msg[msg_max];
    msg[0] = 0x05;
    msg[1] = 0x00;
    bw = snprintf(&msg[2], msg_max, "Connected!");
    msg[bw + 2] = 0x00;
    msg[bw + 3] = 0x00;
    msg[bw + 4] = 11;
    send_string_to_queue(display_queue, msg, bw + 5);
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Lookup our IP
    char ip_addr_string_buff[32] = {0};
    int ip_result = lookup_ip(ip_addr_string_buff);
    if(ip_result<=0){
        ESP_LOGW(TAG,"Failed to get IP!");
    }

    // Show IP
    single_string_rotate(display_queue, ip_addr_string_buff, strlen(ip_addr_string_buff),1);

    // Init sntp
    init_sntp();

    // Start HTTP server — serves the web UI and handles all commands
    start_http_server(display_queue);

    // Exit — HTTP server runs in its own task
    vTaskDelete(NULL);
}

