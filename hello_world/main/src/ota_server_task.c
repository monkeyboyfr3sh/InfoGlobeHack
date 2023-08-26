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

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "errno.h"

#include "message_type.h"
#include "sntp_helper.h"

static const char * TAG = "OTA_TASK";

// Prototypes
static bool is_our_netif(const char *prefix, esp_netif_t *netif);
static int lookup_ip(char *ip_addr_string_buff);
static int setup_tcp_socket(int addr_family, int port);
static bool diagnostic(void);

// Implementations
static bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

#define BUFFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */

/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = { 0 };
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

#define OTA_URL_SIZE 256

static void __attribute__((noreturn)) task_fatal_error(void)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    (void)vTaskDelete(NULL);

    while (1) {
        ;
    }
}

static void print_sha256 (const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hash_print);
}

static void infinite_loop(void)
{
    int i = 0;
    ESP_LOGI(TAG, "When a new firmware is available on the server, press the reset button to download it");
    while(1) {
        ESP_LOGI(TAG, "Waiting for a new firmware ... %d", ++i);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

static void do_retransmit(const int sock)
{
    int len;
    char rx_buffer[128];

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
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");

    ESP_LOGI(TAG,"Getting running partition");
    const esp_partition_t *running = esp_ota_get_running_partition();
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

    esp_err_t err;
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG,"Getting boot partition");
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    if (configured == NULL){
        return ;
        // task_fatal_error();
    }
    
    // if (configured != running) {
    //     ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
    //              configured->address, running->address);
    //     ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    // }
    // ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
    //          running->type, running->subtype, running->address);

    ESP_LOGI(TAG,"Getting update partition");
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL){
        return ;
        // task_fatal_error();
    }
    // ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
    //          update_partition->subtype, update_partition->address);

    int binary_file_length = 0;

    /*deal with all receive packet*/
    bool image_header_was_checked = false;

    int data_read = 0;
    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            return ;
            // task_fatal_error();
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG,rx_buffer,len,ESP_LOG_INFO);

            // Read some data
            data_read += len;
            const size_t header_size = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

            // Do we need to check header?
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                
                // Data read needs to be at least size of header
                if ( data_read > header_size ) {
                    // check current version with downloading
                    memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
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
                            infinite_loop();
                        }
                    }

// #ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
//                     if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
//                         ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
//                         http_cleanup(client);
//                         infinite_loop();
//                     }
// #endif
                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        esp_ota_abort(update_handle);
                        return ;
                        // task_fatal_error();
                    }
                    ESP_LOGI(TAG, "esp_ota_begin succeeded");
                }
                
                else {
                    ESP_LOGW(TAG,"RX %d bytes, min for header is %d bytes", data_read, header_size );
                    // ESP_LOGE(TAG, "received package is not fit len");
                    // esp_ota_abort(update_handle);
                    // task_fatal_error();
                }
            }

            // Header has been checked, can use data
            else {
                // Write data to the partition
                err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
                if (err != ESP_OK) {
                    esp_ota_abort(update_handle);
                    task_fatal_error();
                }

                // Update count, reset data read
                binary_file_length += data_read;
                data_read = 0;
                ESP_LOGD(TAG, "Written image length %d", binary_file_length);
            }
            
            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
            int to_write = len;
            while (to_write > 0) {
                int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    // Failed to retransmit, giving up
                    return;
                }
                to_write -= written;
            }
        }
    } while (len > 0);



    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

    // End ota session
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        return ;
        // task_fatal_error();
    }

    // // Update boot partition
    // err = esp_ota_set_boot_partition(update_partition);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
    //     task_fatal_error();
    // }
    // ESP_LOGI(TAG, "Prepare to restart system!");
    // esp_restart();
    return ;
}

static int setup_tcp_socket(int addr_family, int port)
{
    // Now start TCP stuff
    struct sockaddr_storage dest_addr;
    int ip_protocol = 0;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(port);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return -1;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        return -2;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", port);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        return -2;
    }

    return listen_sock;
}

static int lookup_ip(char *ip_addr_string_buff)
{
    // loop vars
    esp_netif_t *netif = NULL;
    esp_netif_ip_info_t ip;
    int ip_count = 0;

    // Iterate through all interfaces
    for (int i = 0; i < esp_netif_get_nr_of_ifs(); ++i) {
        netif = esp_netif_next(netif);

        // Check if the desc matches us
        if (is_our_netif("example_connect", netif)) {
            
            // Log the IP
            ESP_LOGI(TAG, "Connected to %s", esp_netif_get_desc(netif));
            ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ip));
            sprintf(ip_addr_string_buff,IPSTR, IP2STR(&ip.ip));
            ESP_LOGI(TAG, "- IPv4 address: %s",ip_addr_string_buff);
            
            // Got another!
            ip_count += 1;
        }
    }

    return ip_count;
}

static int listen_for_client(int listen_sock, char * addr_str)
{
    // Config
    int keepAlive = 1;
    int keepIdle = CONFIG_INFOGLOBE_KEEPALIVE_IDLE;
    int keepInterval = CONFIG_INFOGLOBE_KEEPALIVE_INTERVAL;
    int keepCount = CONFIG_INFOGLOBE_KEEPALIVE_COUNT;

    // Wait and listen for client
    struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t addr_len = sizeof(source_addr);
    int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
        return sock;
    }

    // Set tcp keepalive option
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
    
    // Convert ip address to string
    if (source_addr.ss_family == PF_INET) {
        inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
    }
    ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

    return sock;
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

void ota_server_task(void *pvParameters)
{
    // Grab queue
    // QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);

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
        do_retransmit(sock);

        // Cleanup
        shutdown(sock, 0);
        close(sock);
    }

    // Have a socket to cleanup
    if(need_to_cleanup){ close(listen_sock); }

    // Exit
    vTaskDelete(NULL);
}
