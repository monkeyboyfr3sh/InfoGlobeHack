#include "message_type.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char * TAG = "MSG_TYPE";

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

void send_string_to_queue(QueueHandle_t buffer_queue, const char *str_msg, size_t str_len)
{
    // Allocate buffer
    size_t tx_len = str_len;
    uint8_t *init_message_buff = (uint8_t *)malloc(tx_len);
    if (init_message_buff == NULL) {
        ESP_LOGE(TAG,"Failed to allocate memory for buffer\n");
        return;
    }
       
    // Copy data
    memcpy(init_message_buff, str_msg, str_len);

    ESP_LOG_BUFFER_HEX_LEVEL(TAG,init_message_buff,tx_len,ESP_LOG_INFO);

    // Send the buffer to the console task
    push_buffer_to_queue(buffer_queue, init_message_buff, tx_len);

    return;
}

void send_string_w_bytes_to_queue(QueueHandle_t buffer_queue, const char *str_msg, size_t str_len, uint8_t header_byte, uint8_t tail_byte)
{
    // Allocate buffer
    size_t tx_len = str_len+2;
    uint8_t *init_message_buff = (uint8_t *)malloc(tx_len);
    if (init_message_buff == NULL) {
        ESP_LOGE(TAG,"Failed to allocate memory for buffer\n");
        return;
    }
       
    // Copy data
    init_message_buff[0] = header_byte;
    memcpy(&init_message_buff[1], str_msg, str_len);
    init_message_buff[tx_len-1] = tail_byte;

    ESP_LOG_BUFFER_HEX_LEVEL(TAG,init_message_buff,tx_len,ESP_LOG_INFO);

    // Send the buffer to the console task
    push_buffer_to_queue(buffer_queue, init_message_buff, tx_len);

    return;
}