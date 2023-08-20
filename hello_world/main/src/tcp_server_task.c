#include"tcp_server_task.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_timer.h"
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
#include "string.h"

#include "message_type.h"
#include "sntp_helper.h"

static const char * TAG = "TCP_TASK";

// Prototypes
static bool is_our_netif(const char *prefix, esp_netif_t *netif);
static int lookup_ip(char *ip_addr_string_buff);
static void do_retransmit(const int sock, QueueHandle_t display_queue);
static int setup_tcp_socket(int addr_family, int port);

// Implementations
static bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

static void do_retransmit(const int sock, QueueHandle_t display_queue)
{
    int len;
    char rx_buffer[128];

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

            // Allocate buffer
            uint8_t *queue_buffer = (uint8_t *)malloc(len);
            if (queue_buffer == NULL) {
                ESP_LOGE(TAG,"Failed to allocate memory for buffer\n");
                return;
            }

            // Copy data
            memcpy(queue_buffer, rx_buffer, len);

            // Send the buffer to the console task
            push_buffer_to_queue(display_queue, queue_buffer, len);

            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
            int to_write = len;
            while (to_write > 0) {
                int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    // Failed to retransmit, giving up
                    return;
                }
                to_write -= written;
            }
        }
    } while (len > 0);
}

static int setup_tcp_socket(int addr_family, int port)
{
    // Now start TCP stuff
    struct sockaddr_storage dest_addr;
    int ip_protocol = 0;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(port);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return -1;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        return -2;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", port);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        return -2;
    }

    return listen_sock;
}

static int lookup_ip(char *ip_addr_string_buff)
{
    // loop vars
    esp_netif_t *netif = NULL;
    esp_netif_ip_info_t ip;
    int ip_count = 0;

    // Iterate through all interfaces
    for (int i = 0; i < esp_netif_get_nr_of_ifs(); ++i) {
        netif = esp_netif_next(netif);

        // Check if the desc matches us
        if (is_our_netif("example_connect", netif)) {
            
            // Log the IP
            ESP_LOGI(TAG, "Connected to %s", esp_netif_get_desc(netif));
            ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ip));
            sprintf(ip_addr_string_buff,IPSTR, IP2STR(&ip.ip));
            ESP_LOGI(TAG, "- IPv4 address: %s",ip_addr_string_buff);
            
            // Got another!
            ip_count += 1;
        }
    }

    return ip_count;
}

int listen_for_client(int listen_sock, char * addr_str)
{
    // Config
    int keepAlive = 1;
    int keepIdle = CONFIG_INFOGLOBE_KEEPALIVE_IDLE;
    int keepInterval = CONFIG_INFOGLOBE_KEEPALIVE_INTERVAL;
    int keepCount = CONFIG_INFOGLOBE_KEEPALIVE_COUNT;

    // Wait and listen for client
    struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t addr_len = sizeof(source_addr);
    int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
        return sock;
    }

    // Set tcp keepalive option
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
    
    // Convert ip address to string
    if (source_addr.ss_family == PF_INET) {
        inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
    }
    ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

    return sock;
}

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

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    // ESP_ERROR_CHECK(wifi_connect());
    ESP_ERROR_CHECK(example_connect());

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

    // Init sntp
    init_sntp();

    // Setup TCP socket
    bool need_to_cleanup = false;
    int listen_sock = setup_tcp_socket((int)AF_INET, CONFIG_INFOGLOBE_PORT);
    if(listen_sock<0){
        ESP_LOGW(TAG,"Failed to setup listen socket!");

        // Socket was allocated!
        if(listen_sock < -1 ){
            need_to_cleanup = true;
        }
    }

    // Wait and listen for a client
    char addr_str[128];
    int sock = -1;

    while (listen_sock>0) {

        // Listen
        ESP_LOGI(TAG, "Socket listening");
        sock = listen_for_client(listen_sock,addr_str);
        
        // Host session
        do_retransmit(sock, display_queue);

        // Cleanup
        shutdown(sock, 0);
        close(sock);
    }

    // Have a socket to cleanup
    if(need_to_cleanup){ close(listen_sock); }

    // Exit
    vTaskDelete(NULL);
}

