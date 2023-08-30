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
#include "ssh_task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "infoglobe_data_type.h"
#include "infoglobe_animations.h"
#include "message_type.h"
#include "sntp_helper.h"
#include "ssh_task.h"

static const char * TAG = "BUTTON_TASK";

#define COMMAND_MAX_LEN 64
#define NUM_BUTTONS     6
char button_command_lut[NUM_BUTTONS][COMMAND_MAX_LEN] = {
    "qm list",
    "echo 123",
};

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

static int infoglobe_packet_handler(uint8_t *rx_buffer, size_t len)
{
    infoglobe_packet_t packet_buffer;
    infoglobe_packet_types_t packet_type = form_packet_struct(&packet_buffer, rx_buffer, len);
    print_infoglobe_packet(packet_buffer);

    switch (packet_type)
    {
    case control_packet:{
        /* code */
        break;
    }
    case button_packet:{
     
        // Get button index
        int button_idx = rx_buffer[0]-1;
        if(button_idx>=NUM_BUTTONS){
            button_idx = 0;
        }

        // Look for config command
        if( (len>2+NUM_OVERHEAD_BYTES) &&
            (rx_buffer[PACKET_PAYLOAD_INDEX]==1) )
        {
            int cmd_idx = PACKET_PAYLOAD_INDEX+2;
            char * cmd_buffer = (const char *)&rx_buffer[cmd_idx];
            int buffer_len_remain = len-(cmd_idx);
            // Assign button commands
            int cmd_len = strnlen( cmd_buffer, buffer_len_remain) + 1;
            if(cmd_len < COMMAND_MAX_LEN){
                ESP_LOGI(TAG,"Setting command %d to: '%s'", button_idx, cmd_buffer);
                memcpy(&button_command_lut[button_idx][0], cmd_buffer, cmd_len);
            }

            ESP_LOGW(TAG, "Invalid config data");
        }

        // Normal button press
        else {
            char * cmd = &button_command_lut[button_idx][0];
            run_ssh_task_blocked(cmd);
        }
        break;
    }

    default:{
        ESP_LOGW(TAG,"Unknown command type!");
        break;
    }
    }

    return 0;
}

static void do_retransmit(const int sock, QueueHandle_t display_queue)
{
    int len;
    char rx_buffer[128];

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            ESP_LOGI(TAG, "Received %d bytes:", len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG,rx_buffer,len,ESP_LOG_INFO);

            // Put buffer throught he handler
            int ret = infoglobe_packet_handler((uint8_t *)rx_buffer, len);
            
            // Send an ACK
            char * ack_buff = "OK";
            int written = send(sock, ack_buff, strlen(ack_buff), 0);
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

static int listen_for_client(int listen_sock, char * addr_str)
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

void button_tcp_task(void *pvParameters)
{
    // Grab queue
    QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);

    // Setup TCP socket
    bool need_to_cleanup = false;
    int listen_sock = setup_tcp_socket((int)AF_INET, 44444);
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

