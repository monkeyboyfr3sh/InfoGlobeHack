#include"tcp_server_task.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "string.h"

#include "network.h"

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

    /* Ethernet and Wi-Fi come up side by side and keep retrying on their own;
     * continue as soon as either one has an IP. */
    ESP_ERROR_CHECK(network_start());
    while (!network_wait_for_ip(pdMS_TO_TICKS(15000))) {
        ESP_LOGW(TAG, "No network yet (check cable / Wi-Fi), still trying...");
        size_t bw2 = snprintf(connect_msg, msg_max, "No network...");
        send_string_w_bytes_to_queue(display_queue, connect_msg, bw2, 0x00, 0x0);
    }

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

