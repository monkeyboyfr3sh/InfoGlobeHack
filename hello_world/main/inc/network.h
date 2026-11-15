#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/**
 * Bring up the network: Ethernet (if enabled in menuconfig), with Wi-Fi only
 * standing in while Ethernet has no address (see network.c).
 *
 * Non-blocking: interfaces keep (re)connecting in the background. Ethernet
 * failing to initialise (e.g. no PHY) is logged and Wi-Fi carries on alone.
 * Requires esp_netif_init() and the default event loop.
 */
esp_err_t network_start(void);

/** How the globe is on the network now: "lan", "wifi" (standing in for Ethernet) or "none". */
const char *network_link(void);

/** Wait until any interface has an IPv4 address. Returns true if one does. */
bool network_wait_for_ip(TickType_t timeout);

/**
 * Write the preferred IPv4 address (Ethernet first, Wi-Fi as fallback) into
 * buf and remember it as the address currently shown on the globe.
 * Returns "Ethernet" or "Wi-Fi" for the interface used, or NULL if neither
 * currently has an address (buf is then set to "").
 */
const char *network_get_ip_string(char *buf, size_t len);

/**
 * Has the preferred address changed since it was last shown?
 * If so, writes the new address into buf ("" when no interface has one),
 * sets *iface ("Ethernet", "Wi-Fi" or NULL), marks it as shown, and returns
 * true. Cheap enough to poll.
 */
bool network_ip_changed(char *buf, size_t len, const char **iface);

/**
 * The globe's name on the network (settings "hostname"): given to the router
 * with each address request, so routers that list devices by name reach it as
 * "infoglobe" or "infoglobe.lan", and announced as "infoglobe.local" (mDNS).
 * 1-32 of a-z 0-9 and '-', not starting or ending with '-'; anything else (or
 * NULL) means the default, CONFIG_LWIP_LOCAL_HOSTNAME. A change asks for the
 * address again so the router hears the new name. No-op when unchanged.
 */
void network_set_hostname(const char *name);
const char *network_hostname(void);
bool network_hostname_valid(const char *name);
