#pragma once

#include "sdkconfig.h"

// IR LED output (RMT TX with 38.54 kHz carrier), see ir_led_task.c
#define LED_PIN         CONFIG_INFOGLOBE_IR_LED_GPIO

// IR TX Buffer
#define MAX_MESSAGE_LEN 32

void ir_tx_task(void *pvParameters);
