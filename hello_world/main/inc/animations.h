#pragma once

#include "freertos/queue.h"

void run_boot_animation(QueueHandle_t display_queue, uint32_t animation_dur_ms);
void run_animation_1(QueueHandle_t display_queue, uint32_t animation_dur_ms);
void run_animation_2(QueueHandle_t display_queue, uint32_t animation_dur_ms);
void run_hello_world(QueueHandle_t display_queue);
void run_world_domination(QueueHandle_t display_queue);
void run_animation_time_1(QueueHandle_t display_queue);
void string_with_shift_blink(QueueHandle_t display_queue, const char * str_buff, size_t str_len, uint8_t shift_idx, uint8_t blink_idx);
