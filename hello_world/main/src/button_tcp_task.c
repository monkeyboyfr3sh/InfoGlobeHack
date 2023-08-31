#include "frame_maker_task.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "infoglobe_data_type.h"
#include "infoglobe_animations.h"
#include "message_type.h"
#include "sntp_helper.h"
#include "ssh_task.h"
#include "tcp_helpers.h"

static const char * TAG = "BUTTON_TASK";

#define COMMAND_MAX_LEN 64
#define NUM_BUTTONS     6
char button_command_lut[NUM_BUTTONS][COMMAND_MAX_LEN] = {
    "qm list",
    "echo 123",
};

static int infoglobe_packet_handler(uint8_t *rx_buffer, size_t len)
{
    infoglobe_packet_t packet_buffer;
    infoglobe_packet_types_t packet_type = form_packet_struct(&packet_buffer, rx_buffer, len);
    print_infoglobe_packet(packet_buffer);

    switch (packet_type)
    {
    case control_packet:{
        /* code */
        break;
    }
    case button_packet:{
     
        // Get button index
        int button_idx = rx_buffer[PACKET_PAYLOAD_INDEX]-1;
        if(button_idx>=NUM_BUTTONS){
            button_idx = 0;
        }

        // Look for config command
        if( (len>2+NUM_OVERHEAD_BYTES) &&
            (rx_buffer[PACKET_PAYLOAD_INDEX+1]==1) )
        {   
            // Get command index and remaining buffer size
            int cmd_idx = PACKET_PAYLOAD_INDEX+2;
            int buffer_len_remain = len-(cmd_idx);

            // Get pointer of the command start
            char * cmd_buffer = (const char *)&rx_buffer[cmd_idx];

            // Assign button commands
            int cmd_len = strnlen( cmd_buffer, buffer_len_remain) + 1;
            if(cmd_len < COMMAND_MAX_LEN){
                ESP_LOGI(TAG,"Setting command %d to: '%s'", button_idx, cmd_buffer);
                memcpy(&button_command_lut[button_idx][0], cmd_buffer, cmd_len);
            }

            ESP_LOGW(TAG, "Invalid config data");
        }

        // Normal button press
        else {
            char * cmd = &button_command_lut[button_idx][0];
            run_ssh_task_blocked(cmd);
        }
        break;
    }

    default:{
        ESP_LOGW(TAG,"Unknown command type!");
        break;
    }
    }

    return 0;
}

static void do_retransmit(const int sock, QueueHandle_t display_queue)
{
    int len;
    char rx_buffer[128];

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            ESP_LOGI(TAG, "Received %d bytes:", len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG,rx_buffer,len,ESP_LOG_INFO);

            // Put buffer throught he handler
            int ret = infoglobe_packet_handler((uint8_t *)rx_buffer, len);
            
            // Send an ACK
            char * ack_buff = "OK";
            int written = send(sock, ack_buff, strlen(ack_buff), 0);
        }
    } while (len > 0);
}

void button_tcp_task(void *pvParameters)
{
    // Grab queue
    QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);

    // Setup TCP socket
    bool need_to_cleanup = false;
    int listen_sock = setup_tcp_socket((int)AF_INET, 44444);
    if(listen_sock<0){
        ESP_LOGW(TAG,"Failed to setup listen socket!");

        // Socket was allocated!
        if(listen_sock < -1 ){
            need_to_cleanup = true;
        }
    }

    // Wait and listen for a client
    char addr_str[128];
    int sock = -1;

    while (listen_sock>0) {

        // Listen
        ESP_LOGI(TAG, "Socket listening");
        sock = listen_for_client(listen_sock,addr_str);
        
        // Host session
        do_retransmit(sock, display_queue);

        // Cleanup
        shutdown(sock, 0);
        close(sock);
    }

    // Have a socket to cleanup
    if(need_to_cleanup){ close(listen_sock); }

    // Exit
    vTaskDelete(NULL);
}

