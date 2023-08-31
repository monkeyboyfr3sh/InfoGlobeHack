#pragma once

#include <stdint.h>
#include "esp_netif.h"

bool is_our_netif(const char *prefix, esp_netif_t *netif);
int setup_tcp_socket(int addr_family, int port);
int lookup_ip(char *ip_addr_string_buff);
int listen_for_client(int listen_sock, char * addr_str);
