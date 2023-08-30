#include "infoglobe_data_type.h"
#include "esp_log.h"

static const char* TAG = "INFOGLOBE_DATA";

infoglobe_packet_types_t lookup_command_bye(uint8_t cmd_byte)
{
    // Lookup command type based on the byte
    switch (cmd_byte)
    {
    case control_cmd_byte:{
        return control_packet;
        break;
    }
    case button_cmd_byte:{
        return button_packet;
        break;
    }
    default:
        return unknown_packet;
        break;
    }
}

infoglobe_packet_types_t form_packet_struct(infoglobe_packet_t *packet_buff, uint8_t *buffer, size_t buffer_len)
{
    // Lookup command
    packet_buff->packet_type = lookup_command_bye(buffer[PACKET_TYPE_INDEX]);
    // Update buff and len
    packet_buff->packet_buff = buffer;
    packet_buff->packet_len = buffer_len;

    return packet_buff->packet_type;
}

void print_infoglobe_packet(infoglobe_packet_t packet_buff)
{
    ESP_LOGI(TAG, "Packet Type:     %d",packet_buff.packet_type);
    ESP_LOGI(TAG, "Packet Byte:     0x%d",packet_buff.packet_buff[PACKET_TYPE_INDEX]);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG,packet_buff.packet_buff,packet_buff.packet_len,ESP_LOG_INFO);
}