#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/**
 * Bring up Ethernet (if enabled in menuconfig) and Wi-Fi side by side.
 *
 * Non-blocking: both interfaces start immediately and keep (re)connecting in
 * the background. Ethernet failing to initialise (e.g. no PHY) is logged and
 * Wi-Fi carries on alone. Requires esp_netif_init() and the default event loop.
 */
esp_err_t network_start(void);

/** Wait until any interface has an IPv4 address. Returns true if one does. */
bool network_wait_for_ip(TickType_t timeout);

/**
 * Write the preferred IPv4 address (Ethernet first, then Wi-Fi) into buf.
 * Returns "Ethernet" or "Wi-Fi" for the interface used, or NULL if neither
 * currently has an address (buf is then set to "").
 */
const char *network_get_ip_string(char *buf, size_t len);
