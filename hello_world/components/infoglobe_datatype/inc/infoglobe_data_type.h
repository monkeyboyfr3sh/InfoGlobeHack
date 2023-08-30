#pragma once

#include <string.h>
#include <stdint.h>
#include "infoglobe_byte.h"

#define PACKET_TYPE_INDEX       0
#define PACKET_PAYLOAD_INDEX    1
#define NUM_OVERHEAD_BYTES      1

typedef enum infoglobe_packet_types_t {
    control_packet,
    button_packet,
    unknown_packet
}infoglobe_packet_types_t;

typedef struct infoglobe_packet_t {
    infoglobe_packet_types_t packet_type;
    uint8_t *packet_buff;
    size_t packet_len;
} infoglobe_packet_t;

infoglobe_packet_types_t lookup_command_bye(uint8_t cmd_byte);
infoglobe_packet_types_t form_packet_struct(infoglobe_packet_t *packet_buff, uint8_t *buffer, size_t buffer_len);
void print_infoglobe_packet(infoglobe_packet_t packet_buff);