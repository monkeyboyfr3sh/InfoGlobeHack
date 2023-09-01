#include "ota_server_task.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include <sys/param.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "string.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "errno.h"

#include "infoglobe_animations.h"
#include "message_type.h"
#include "sntp_helper.h"
#include "tcp_helpers.h"

static const char * TAG = "OTA_TASK";

#define BUFFSIZE 8192
#define HASH_LEN 32 /* SHA-256 digest length */

// Prototypes
static bool diagnostic(void);
static void system_reboot(QueueHandle_t display_queue);

static void print_sha256 (const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hash_print);
}

static int check_image_header(uint8_t *data_buffer)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t new_app_info;
    
    // check current version with downloading
    memcpy(&new_app_info, 
        &data_buffer[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)],
        sizeof(esp_app_desc_t));
    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
    esp_app_desc_t invalid_app_info;
    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
    }

    // check current version with last invalid partition
    if (last_invalid_app != NULL) {
        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
            ESP_LOGW(TAG, "New version is the same as invalid version.");
            ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
            ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
            return -1;
        }
    }

// #ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
//                     if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
//                         ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
//                         http_cleanup(client);
//                         infinite_loop();
//                     }
// #endif

    return 0;
}

static void print_ota_partitions(esp_partition_t *running, esp_partition_t *configured, esp_partition_t *update_partition)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for the partition table
    partition.address   = ESP_PARTITION_TABLE_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_MAX_LEN;
    partition.type      = ESP_PARTITION_TYPE_DATA;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for the partition table: ");

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(running, sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");

    ESP_LOGI(TAG,"Getting running partition");
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // run diagnostic function ...
            bool diagnostic_is_ok = diagnostic();
            if (diagnostic_is_ok) {
                ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
                esp_ota_mark_app_valid_cancel_rollback();
            } else {
                ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }

    // Configured i snot running
    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }

    // Onward
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);

}

int get_ota_header(const int sock, uint8_t *rx_buffer, size_t buffer_size)
{
    if(rx_buffer == NULL){
        return -1;
    }

    const size_t header_size = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    size_t rx_len = 0;
    size_t header_len = 0;
    while(1)
    {
        // Get data from the socket
        rx_len += recv(sock, (void *)&rx_buffer[header_len], (buffer_size-header_len), 0);
        
        // Handle error
        if (rx_len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            return -1;
        } else if (rx_len == 0) {
            ESP_LOGW(TAG, "Connection closed");
            return -2;
        } 

        // Update size
        header_len += rx_len;

        // Got enough yet?
        if ( header_len > header_size ) {
            break;
        }

        else {
            ESP_LOGW(TAG,"RX %d bytes, min for header is %d bytes", rx_len, header_size );
        }

        // Send an ACK
        char * ack_buff = "ACK";
        int written = send(sock, ack_buff, strlen(ack_buff), 0);
        written = send(sock, ack_buff, strlen(ack_buff), 0);
    }

    // Success!
    return header_len;
}

int recv_ota(const int sock, uint8_t *rx_buffer, size_t buffer_size, QueueHandle_t display_queue, const esp_partition_t *update_handle)
{
    // Time when OTA really started
    TickType_t ota_start_time = xTaskGetTickCount();
    TickType_t ota_status_print_time = xTaskGetTickCount();

    // Show update start on display
    const char *ota_start_msg = "< Starting OTA >";
    string_with_blink_shift(display_queue,
        ota_start_msg, strlen(ota_start_msg),
        strlen(ota_start_msg)+2, strlen(ota_start_msg)+2);


    char * ack_buff = "ACK";
    size_t ota_len = 0;
    size_t rx_len = 0;
    while(1)
    {
        // Get data from the socket
        rx_len = recv(sock, rx_buffer, buffer_size, 0);
        
        // Handle error
        if (rx_len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            return -1;
        } else if (rx_len == 0) {
            ESP_LOGW(TAG, "Connection closed");
            return -2;
        } 

        // Header has been checked, can use data
        esp_err_t err = esp_ota_write( update_handle, (const void *)rx_buffer, rx_len);
        if (err != ESP_OK) {
            return -3;
            break;
        }

        // Update counter
        ota_len += rx_len;
        // Send an ACK
        int written = send(sock, ack_buff, strlen(ack_buff), 0);

        // Update count, reset data read
        ESP_LOGD(TAG, "Written image length %d", ota_len);

        // After time, start showing percent progress
        if( xTaskGetTickCount()-ota_start_time > pdMS_TO_TICKS(8000)) {

            if( xTaskGetTickCount()-ota_status_print_time > pdMS_TO_TICKS(1000) )
            {
                ota_status_print_time = xTaskGetTickCount();
                int runtime = (int)( ( pdTICKS_TO_MS( xTaskGetTickCount()-ota_start_time ) / 1000 ) );
                // Update Display
                char progress_msg[128] = {0};
                snprintf(progress_msg,sizeof(progress_msg),"Updating... (%ds)", runtime);
                string_with_blink_shift(display_queue,
                    progress_msg, strlen(progress_msg),
                    0, strlen(progress_msg)+4);
            }

        }   
    }
}


static int do_retransmit(const int sock, QueueHandle_t display_queue)
{
    esp_err_t err;

    // Get partitions
    ESP_LOGI(TAG,"Getting boot partition");
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG,"Getting configured partition");
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    ESP_LOGI(TAG,"Getting update partition");
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t update_handle = NULL;

    // Check for empty pointers
    if ( (running == NULL) || (configured == NULL) || (update_partition == NULL) ){
        return -1;
    }
    
    // Print running partition
    print_ota_partitions(running,configured,update_partition);

    // Start the update process
    int binary_file_length = 0;    
    size_t len;
    char * rx_buffer = malloc(sizeof(char)*BUFFSIZE);
    int ret_status = 0;

    while (1)
    {   
        // Get header
        ESP_LOGI(TAG,"Getting header");
        int header_length = get_ota_header(sock, (uint8_t *)rx_buffer, BUFFSIZE);
        if(header_length<=0){
            ESP_LOGW(TAG,"Error getting header");
            ret_status = -1;
            break;
        }

        // Check the header
        ESP_LOGI(TAG,"Checking header");
        int image_header_status = check_image_header((uint8_t *)rx_buffer);
        if(image_header_status){
            ESP_LOGW(TAG,"Error with header");
            ret_status = -2;
            break;
        }

        // Start OTA
        ESP_LOGI(TAG,"Starting OTA");
        err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
            ret_status = -3;
            break;
        }
        ESP_LOGI(TAG, "esp_ota_begin succeeded");

        // Header has been checked, can use data
        ESP_LOGI(TAG,"Writing Header...");
        err = esp_ota_write( update_handle, (const void *)rx_buffer, header_length);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,"Error writing the header!");
            ret_status = -4;
            break;
        }

        // Receive OTA
        ESP_LOGI(TAG,"Receiving the rest of OTA");
        int ota_len = recv_ota(sock, (uint8_t *)rx_buffer, BUFFSIZE, display_queue, update_handle);
        if(ota_len<0){
            ESP_LOGW(TAG,"Error Rceiving ota!");
            ret_status = -5;
            break;
        }

        // Can now exit
        break;
    }

    // Need to free rx_buffer
    free(rx_buffer);

    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

    // Did we detect errror?
    if( ret_status ){
        // Check if we have handle to abort update from
        if(update_handle){
            esp_ota_abort(update_handle);
        }
        return -6;
    }


    // End ota session
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        free(rx_buffer);
        return -7;
    }

    // Update boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        free(rx_buffer);
        return -8;
    }

    return 0;
}

static bool diagnostic(void)
{
    // gpio_config_t io_conf;
    // io_conf.intr_type    = GPIO_INTR_DISABLE;
    // io_conf.mode         = GPIO_MODE_INPUT;
    // io_conf.pin_bit_mask = (1ULL << CONFIG_EXAMPLE_GPIO_DIAGNOSTIC);
    // io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    // gpio_config(&io_conf);

    // ESP_LOGI(TAG, "Diagnostics (5 sec)...");
    // vTaskDelay(5000 / portTICK_PERIOD_MS);

    // bool diagnostic_is_ok = gpio_get_level(CONFIG_EXAMPLE_GPIO_DIAGNOSTIC);

    // gpio_reset_pin(CONFIG_EXAMPLE_GPIO_DIAGNOSTIC);

    return true;
}

static void system_reboot(QueueHandle_t display_queue)
{
    ESP_LOGI(TAG, "Prepare to restart system!");
    for(int i = 3;i>0;i--){
        ESP_LOGI(TAG,"Restarting in %d...",i);
        // Update Display
        char reboot_msg[128] = {0};
        snprintf(reboot_msg,sizeof(reboot_msg),"Reboot in %d", i);
        string_with_blink_shift(display_queue,
            reboot_msg, strlen(reboot_msg),
            0, strlen(reboot_msg));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_restart();
}

void ota_server_task(void *pvParameters)
{
    // Grab queue
    QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);

    const int ota_port = 22222;

    // Lookup our IP
    char ip_addr_string_buff[32] = {0};
    int ip_result = lookup_ip(ip_addr_string_buff);
    if(ip_result<=0){
        ESP_LOGW(TAG,"Failed to get IP!");
    }

    // Setup TCP socket
    bool need_to_cleanup = false;
    int listen_sock = setup_tcp_socket((int)AF_INET, ota_port);
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
        int ota_ret = do_retransmit(sock, display_queue);
        
        // Send an ACK
        char * ack_buff = "ACK";
        int written = send(sock, ack_buff, strlen(ack_buff), 0);

        // Error with ota
        if(ota_ret){
            ESP_LOGW(TAG,"OTA exits with: %d", ota_ret);
        
            // Send a NAK
            char * nak_buff = "NAK";
            int written = send(sock, nak_buff, strlen(nak_buff), 0);

            // Cleanup
            shutdown(sock, 0);
            close(sock);
        }

        // Successful ota
        else {
            
            // Send a ACK
            char * ack_buff = "ACK";
            int written = send(sock, ack_buff, strlen(ack_buff), 0);

            // Cleanup
            shutdown(sock, 0);
            close(sock);

            // Exits
            break;
        }
    }

    // Have a socket to cleanup
    if(need_to_cleanup){ close(listen_sock); }

    // Reboot
    system_reboot(display_queue);

    // Exit
    vTaskDelete(NULL);
}
