#pragma once

/* Optional access token for everything that changes the globe.
 *
 * No token set (the default): every request is allowed, as before.
 * Token set: requests that change something must carry it, in one of
 *   Authorization: Bearer <token>
 *   X-Globe-Token: <token>
 *   ?token=<token>         (webhook senders that can't set headers)
 * The first one present is the one checked. Reading (GET, the WebSocket)
 * stays open.
 *
 * The token lives in NVS (namespace "auth"), not in settings.json, and no
 * endpoint ever returns it. Holding the globe's button for 10 s clears it. */

#include <stdbool.h>
#include "esp_http_server.h"

#define AUTH_TOKEN_MIN  8
#define AUTH_TOKEN_MAX  64      // characters: A-Z a-z 0-9 . _ ~ - (safe in a URL as is)

/* CORS: the web app on a dev server sends the token in a header. */
#define AUTH_CORS_HEADERS "Content-Type, Authorization, X-Globe-Token"

void auth_init(void);                   // load from NVS; call after nvs_flash_init()
bool auth_required(void);               // a token is set
bool auth_token_valid(const char *t);   // length and characters ("" is valid: it means remove)
bool auth_set(const char *token);       // save; "" removes it. false: invalid token or NVS error
void auth_clear(void);                  // recovery (button hold): remove the token

/* For handlers that change something. true: go ahead. false: a 401
 * {"ok":false,"error":...} has been sent; return ESP_OK from the handler. */
bool auth_check(httpd_req_t *req);

/* Same test without answering: the request carries the right token, or none is needed. */
bool auth_request_ok(httpd_req_t *req);
