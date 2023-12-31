#include "ir_led_task.h"

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
#include "esp_event.h"
#include "nvs_flash.h"

#include "string.h"

#include "message_type.h"

static const char * TAG = "IR_TASK";

#define PWM_DUTY        128
#define BIT_PERIOD_MS   1000

// Globals
uint8_t message[MAX_MESSAGE_LEN] = {0}; // Example message
int tx_bit_index = 0;
int tx_byte_index = 0;
int tx_len = 0;

// Function prototypes
static void timer_callback(void *arg);
static void initalize_message(void);

// OLYMPIA INFOGLOBE
void ir_tx_task(void *pvParameters)
{
    // Grab queue
    QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);

    // Configure LEDC timer for 38 kHz
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER,
        .freq_hz = 38540,
    };
    ledc_timer_config(&ledc_timer);

    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .gpio_num = LED_PIN,
        .speed_mode = LEDC_SPEED_MODE,
        .channel = LED_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = PWM_DUTY,  // 50% duty cycle for a square wave
    };
    ledc_channel_config(&ledc_channel);

    // Initialize message
    initalize_message();
    
    const esp_timer_create_args_t my_timer_args = {
        .callback = &timer_callback,
        .name = "My Timer"};
    esp_timer_handle_t timer_handler;
    ESP_ERROR_CHECK(esp_timer_create(&my_timer_args, &timer_handler));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handler, BIT_PERIOD_MS));

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
            if (pop_buffer_from_queue(display_queue, &received_buffer_struct) == pdPASS) {
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
                ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handler, BIT_PERIOD_MS));

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
        ledc_set_duty(LEDC_SPEED_MODE, LED_CHANNEL, PWM_DUTY);
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

static void initalize_message(void)
{
 
    // Zero memory
    memset(message,0,MAX_MESSAGE_LEN);

    // Copy your original message into the middle of the array
    const char *temp_message = "OLYMPIA INFOGLOBE";
    memcpy(&message[1],temp_message,strlen(temp_message));
}