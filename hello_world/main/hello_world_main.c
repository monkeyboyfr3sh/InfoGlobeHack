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

static const char * TAG = "MAIN";

// Definitions
// #define CONFIG_INFOGLOBE_PORT                        5555
#define KEEPALIVE_IDLE              CONFIG_EXAMPLE_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_EXAMPLE_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_EXAMPLE_KEEPALIVE_COUNT

// PWM Driver
#define LED_PIN         GPIO_NUM_16
#define LED_CHANNEL     LEDC_CHANNEL_0
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_SPEED_MODE LEDC_HIGH_SPEED_MODE

// Timer ISR
#define TIMER_DIVIDER   80  // 80 MHz divided by 80 = 1 MHz
#define TIMER_SCALE     (TIMER_BASE_CLK / TIMER_DIVIDER) // convert counter value to seconds

// IR TX Buffer
#define MAX_MESSAGE_LEN 32

// Data Prototypes
typedef struct {
    size_t size;
    uint8_t *buffer;
} msg_buffer_t;

// Prototypes 
static void timer_callback(void *arg);
static esp_err_t wifi_connect(void);
static void do_retransmit(const int sock);
void tcp_server_task(void *pvParameters);
void initalize_message(void);

void ir_tx_task(void *pvParameters);

void push_buffer_to_queue(uint8_t *data, size_t size);
BaseType_t pop_buffer_from_queue(msg_buffer_t *received_buffer_struct);

// Globals
uint8_t message[MAX_MESSAGE_LEN] = {0}; // Example message
int tx_bit_index = 0;
int tx_byte_index = 0;
int tx_len = 0;
QueueHandle_t buffer_queue;
char ip_addr_string_buff[MAX_MESSAGE_LEN] = {0};

static bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    // ESP_ERROR_CHECK(wifi_connect());
    ESP_ERROR_CHECK(example_connect());

    // Init buffer queue
    buffer_queue = xQueueCreate(5, sizeof(msg_buffer_t));
    if (buffer_queue == NULL) {
        printf("Failed to create queue\n");
        return;
    }

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

    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
    xTaskCreate(ir_tx_task, "ir_tx", 4096, (void*)AF_INET, 5, NULL);
}

void push_buffer_to_queue(uint8_t *data, size_t size)
{
    msg_buffer_t msg_buffer_t;
    msg_buffer_t.size = size;
    msg_buffer_t.buffer = data;
    
    xQueueSend(buffer_queue, &msg_buffer_t, pdMS_TO_TICKS(100));
}

BaseType_t pop_buffer_from_queue(msg_buffer_t *received_buffer_struct)
{
    return xQueueReceive(buffer_queue, received_buffer_struct, portMAX_DELAY);
}
// OLYMPIA INFOGLOBE
void ir_tx_task(void *pvParameters)
{
    // Configure LEDC timer for 38 kHz
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER,
        .freq_hz = 38460,
    };
    ledc_timer_config(&ledc_timer);

    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .gpio_num = LED_PIN,
        .speed_mode = LEDC_SPEED_MODE,
        .channel = LED_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 100,  // 50% duty cycle for a square wave
    };
    ledc_channel_config(&ledc_channel);

    // Initialize message
    initalize_message();
    
    const esp_timer_create_args_t my_timer_args = {
        .callback = &timer_callback,
        .name = "My Timer"};
    esp_timer_handle_t timer_handler;
    ESP_ERROR_CHECK(esp_timer_create(&my_timer_args, &timer_handler));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handler, 1000));

    // Set display to the IP addresss
    // Allocate buffer
    uint8_t len = strlen(ip_addr_string_buff)+2;
    uint8_t *queue_buffer = (uint8_t *)malloc(len);
    if (queue_buffer == NULL) {
        ESP_LOGE(TAG,"Failed to allocate memory for buffer\n");
        return;
    }

    // Copy data
    queue_buffer[0] = 0x03;
    memcpy(&queue_buffer[1], ip_addr_string_buff, strlen(ip_addr_string_buff));
    queue_buffer[len-1] = 0x5f;

    // Send the buffer to the console task
    push_buffer_to_queue(queue_buffer, len);

    msg_buffer_t received_buffer_struct;
    while (1) {
        // Start new tx session
        if (tx_len == 0) {

            // Stop the strobe
            ledc_set_duty(LEDC_SPEED_MODE, LED_CHANNEL, 0);
            ledc_update_duty(LEDC_SPEED_MODE, LED_CHANNEL);
            ESP_ERROR_CHECK(esp_timer_stop(timer_handler));
            
            ESP_LOGI(TAG,"TX Complete!");

            // Wait for 3s
            if (pop_buffer_from_queue(&received_buffer_struct) == pdPASS) {
                vTaskDelay(pdMS_TO_TICKS(100));
                printf("Received buffer of size %d: ", received_buffer_struct.size);
                for (size_t i = 0; i < received_buffer_struct.size; i++) {
                    printf("%d ", received_buffer_struct.buffer[i]);
                }
                printf("\n");

                // New TX session!

                // Zero memory
                memset(message,0,MAX_MESSAGE_LEN);
                // Copy data
                tx_len = received_buffer_struct.size;
                tx_byte_index = 0;
                tx_bit_index = 0;
                memcpy(&message[0], received_buffer_struct.buffer, tx_len);

                // Leading and trailing zeros
                ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handler, 1000));

                ESP_LOGI(TAG,"TX Begin!");

                // Free the allocated memory
                free(received_buffer_struct.buffer);
            }
        }
    }
}

static void timer_callback(void *param)
{
    uint8_t current_byte = message[tx_byte_index];
    bool bit = current_byte & (1 << (7-tx_bit_index));
    printf("%d", bit);
    if (bit) {
        ledc_set_duty(LEDC_SPEED_MODE, LED_CHANNEL, 0);
    } else {
        ledc_set_duty(LEDC_SPEED_MODE, LED_CHANNEL, 100);
    }
    ledc_update_duty(LEDC_SPEED_MODE, LED_CHANNEL);

    tx_bit_index++;
    if (tx_bit_index >= 8) {
        printf(" | 0x%x | '%c'\r\n",current_byte, current_byte);
        tx_bit_index = 0;
        tx_byte_index++;
        tx_len--;
    }
}

void initalize_message(void)
{
 
    // Zero memory
    memset(message,0,MAX_MESSAGE_LEN);

    // Copy your original message into the middle of the array
    const char *temp_message = "OLYMPIA INFOGLOBE";
    memcpy(&message[1],temp_message,strlen(temp_message));
}

static void do_retransmit(const int sock)
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
            push_buffer_to_queue(queue_buffer, len);

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

void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(CONFIG_INFOGLOBE_PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", CONFIG_INFOGLOBE_PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
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

        do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

