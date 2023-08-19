#include "message_type.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

void push_buffer_to_queue(QueueHandle_t buffer_queue, uint8_t *data, size_t size)
{
    msg_buffer_t msg_buffer_t;
    msg_buffer_t.size = size;
    msg_buffer_t.buffer = data;
    
    xQueueSend(buffer_queue, &msg_buffer_t, pdMS_TO_TICKS(100));
}

BaseType_t pop_buffer_from_queue(QueueHandle_t buffer_queue, msg_buffer_t *received_buffer_struct)
{
    return xQueueReceive(buffer_queue, received_buffer_struct, portMAX_DELAY);
}