#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_http_server.h"

httpd_handle_t start_http_server(QueueHandle_t display_queue);
