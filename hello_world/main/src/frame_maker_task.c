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
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "wifi_connect.h"
#include "message_type.h"
#include "tcp_server_task.h"

#include "message_type.h"

static const char * TAG = "FR_MKR";

char ip_addr_string_buff[32] = {0};

static bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

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

    // Set display to the IP addresss
    const size_t msg_max = 32;
    char connect_msg[msg_max];
    size_t bw = snprintf(connect_msg,msg_max,"Connecting...");
    send_string_w_bytes_to_queue(display_queue, connect_msg, bw, 0x00, 0x0);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    // ESP_ERROR_CHECK(wifi_connect());
    ESP_ERROR_CHECK(example_connect());

    esp_netif_t *netif = NULL;
    esp_netif_ip_info_t ip;
    for (int i = 0; i < esp_netif_get_nr_of_ifs(); ++i) {
        netif = esp_netif_next(netif);
        if (is_our_netif("example_connect", netif)) {
            ESP_LOGI(TAG, "Connected to %s", esp_netif_get_desc(netif));
            ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ip));
            sprintf(ip_addr_string_buff,IPSTR, IP2STR(&ip.ip));
            ESP_LOGI(TAG, "- IPv4 address: %s",ip_addr_string_buff);
        }
    }

    // Set display to the IP addresss
    send_string_w_bytes_to_queue(display_queue, ip_addr_string_buff, strlen(ip_addr_string_buff), 0x00, 0x10);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Create TCP server thread
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)display_queue, 5, NULL);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}