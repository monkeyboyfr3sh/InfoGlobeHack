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

#include "tcp_server_task.h"
#include "message_type.h"

static const char * TAG = "MAIN";

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

// Prototypes 
static void timer_callback(void *arg);
void initalize_message(void);
void ir_tx_task(void *pvParameters);

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
    uint8_t *init_message_buff = (uint8_t *)malloc(len);
    if (init_message_buff == NULL) {
        ESP_LOGE(TAG,"Failed to allocate memory for buffer\n");
        return;
    }

    // Copy data
    init_message_buff[0] = 0x03;
    memcpy(&init_message_buff[1], ip_addr_string_buff, strlen(ip_addr_string_buff));
    init_message_buff[len-1] = 0x5f;

    // Send the buffer to the console task
    push_buffer_to_queue(buffer_queue, init_message_buff, len);

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
            if (pop_buffer_from_queue(buffer_queue, &received_buffer_struct) == pdPASS) {
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
