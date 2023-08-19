#pragma once

#include "freertos/queue.h"

void run_boot_animation(QueueHandle_t display_queue);
void run_animation_1(QueueHandle_t display_queue);
void run_animation_2(QueueHandle_t display_queue);