#include "stdint.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "string.h"
#include "freertos/queue.h"

// Data Prototypes
typedef struct {
    size_t size;
    uint8_t *buffer;
} msg_buffer_t;

BaseType_t pop_buffer_from_queue(QueueHandle_t buffer_queue, msg_buffer_t *received_buffer_struct);
void push_buffer_to_queue(QueueHandle_t buffer_queue, uint8_t *data, size_t size);