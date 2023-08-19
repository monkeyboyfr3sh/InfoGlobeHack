#pragma once

// PWM Driver
#define LED_PIN         GPIO_NUM_16
#define LED_CHANNEL     LEDC_CHANNEL_0
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE

// Timer ISR
#define TIMER_DIVIDER   80  // 80 MHz divided by 80 = 1 MHz
#define TIMER_SCALE     (TIMER_BASE_CLK / TIMER_DIVIDER) // convert counter value to seconds

// IR TX Buffer
#define MAX_MESSAGE_LEN 32

void ir_tx_task(void *pvParameters);