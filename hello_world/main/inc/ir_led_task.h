#pragma once

#include <stdint.h>
#include "sdkconfig.h"

// IR LED output (RMT TX with 38.54 kHz carrier), see ir_led_task.c
#define LED_PIN         CONFIG_INFOGLOBE_IR_LED_GPIO

// Largest frame payload (bytes) and largest idle gap between frames
#define MAX_MESSAGE_LEN 32
#define IR_MAX_GAP_MS   500

typedef struct {
    float    fps;          // frames per second over the last window
    float    busy_pct;     // % of the window the link was transmitting (bits + gaps)
    uint32_t frames;       // total frames sent since boot
    uint32_t gap_ms;       // current idle gap appended after each frame
} ir_stats_t;

void ir_tx_task(void *pvParameters);

/* Idle time after every frame, so the globe can find the start of the next
 * one (the protocol has no start/stop bits). Applies from the next frame. */
void     ir_set_frame_gap_ms(uint32_t gap_ms);
uint32_t ir_get_frame_gap_ms(void);

void ir_get_stats(ir_stats_t *out);
