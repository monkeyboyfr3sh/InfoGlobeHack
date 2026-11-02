#pragma once

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Registers the /api/v1 routes and /designer on an already started server.
void api_v1_register(httpd_handle_t server, QueueHandle_t display_queue);
