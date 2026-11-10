#pragma once

/* Live mirror: GET /api/v1/ws (WebSocket) pushes "now showing" and the pushed
 * values to browsers as they change. Read-only; see live.c for the protocol. */

#include "esp_http_server.h"

#define LIVE_MAX_CLIENTS  2     // one more gets close code 1013 and should poll instead

void live_register(httpd_handle_t server);
