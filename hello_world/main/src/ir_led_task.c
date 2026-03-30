#include "ir_led_task.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gptimer.h"
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
#define BIT_PERIOD_US   1000  // 1 ms in microseconds

// Globals — volatile because accessed from both ISR and task
static volatile uint8_t message[MAX_MESSAGE_LEN] = {0};
static volatile int tx_bit_index = 0;
static volatile int tx_byte_index = 0;
static volatile int tx_len = 0;

// Function prototypes
static bool IRAM_ATTR timer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx);
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
        .duty = PWM_DUTY,
    };
    ledc_channel_config(&ledc_channel);

    // Initialize message
    initalize_message();

    // Configure hardware timer — ISR context, unaffected by task scheduling
    gptimer_handle_t timer_handler = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 MHz → 1 µs per tick
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &timer_handler));

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = BIT_PERIOD_US,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer_handler, &alarm_config));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer_handler, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(timer_handler));
    ESP_ERROR_CHECK(gptimer_start(timer_handler));

    msg_buffer_t received_buffer_struct;
    while (1) {
        // Start new tx session
        if (tx_len == 0) {

            // Stop the strobe
            ledc_set_duty(LEDC_SPEED_MODE, LED_CHANNEL, 0);
            ledc_update_duty(LEDC_SPEED_MODE, LED_CHANNEL);
            ESP_ERROR_CHECK(gptimer_stop(timer_handler));

            ESP_LOGI(TAG,"TX Complete!");

            if (pop_buffer_from_queue(display_queue, &received_buffer_struct) == pdPASS) {
                vTaskDelay(pdMS_TO_TICKS(100));
                printf("Received buffer of size %d: ", received_buffer_struct.size);
                for (size_t i = 0; i < received_buffer_struct.size; i++) {
                    printf("%d ", received_buffer_struct.buffer[i]);
                }
                printf("\n");

                // New TX session
                memset((void*)message, 0, MAX_MESSAGE_LEN);
                tx_len = received_buffer_struct.size;
                tx_byte_index = 0;
                tx_bit_index = 0;
                memcpy((void*)message, received_buffer_struct.buffer, tx_len);

                ESP_ERROR_CHECK(gptimer_start(timer_handler));
                ESP_LOGI(TAG,"TX Begin!");

                free(received_buffer_struct.buffer);
            }
        } else {
            // ISR handles transmission; yield so IDLE can run
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static bool IRAM_ATTR timer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    if (tx_len <= 0) {
        ledc_set_duty(LEDC_SPEED_MODE, LED_CHANNEL, 0);
        ledc_update_duty(LEDC_SPEED_MODE, LED_CHANNEL);
        return false;
    }

    uint8_t current_byte = message[tx_byte_index];
    bool bit = current_byte & (1 << (7 - tx_bit_index));
    if (bit) {
        ledc_set_duty(LEDC_SPEED_MODE, LED_CHANNEL, 0);
    } else {
        ledc_set_duty(LEDC_SPEED_MODE, LED_CHANNEL, PWM_DUTY);
    }
    ledc_update_duty(LEDC_SPEED_MODE, LED_CHANNEL);

    tx_bit_index++;
    if (tx_bit_index >= 8) {
        tx_bit_index = 0;
        tx_byte_index++;
        tx_len--;
    }

    return false; // no high-priority task woken
}

static void initalize_message(void)
{
    memset((void*)message, 0, MAX_MESSAGE_LEN);
    const char *temp_message = "OLYMPIA INFOGLOBE";
    memcpy((void*)&message[1], temp_message, strlen(temp_message));
}
