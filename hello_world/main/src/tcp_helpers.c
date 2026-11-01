#include "tcp_helpers.h"

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
// #include "ssh_task.h"

#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "network.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "infoglobe_data_type.h"
#include "infoglobe_animations.h"
#include "message_type.h"
#include "sntp_helper.h"
// #include "ssh_task.h"

static const char * TAG = "TCP_HELP";

// Implementations
bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

int setup_tcp_socket(int addr_family, int port)
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

// Preferred IPv4 (Ethernet first, then Wi-Fi). Buffer must hold 16 chars.
// Returns 1 if an address was written, 0 if no interface has one.
int lookup_ip(char *ip_addr_string_buff)
{
    const char *iface = network_get_ip_string(ip_addr_string_buff, 16);
    if (iface == NULL) {
        return 0;
    }
    ESP_LOGI(TAG, "Connected via %s, IPv4 address: %s", iface, ip_addr_string_buff);
    return 1;
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