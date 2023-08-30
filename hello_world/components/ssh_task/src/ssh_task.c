#include "ssh_task.h"

#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "netdb.h" // gethostbyname

#include "libssh2_config.h"
#include <libssh2.h>
#include <libssh2_sftp.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "esp_err.h"
#include "driver/uart.h"

#include "ssh_driver.h"

#define UART_NUM UART_NUM_0

static const char *TAG = "SSH_DRIVER";

static void ssh_task_exit(ssh_task_input_t * task_parameter);
static void ssh_task_fail(ssh_task_input_t * task_parameter);

void ssh_task(void *pvParameters)
{
    // null input check
    if(pvParameters==NULL){
        ESP_LOGE(TAG,"Null param!");
	    vTaskDelete( NULL );
    }

    // Get input
	ssh_task_input_t * task_parameter = (ssh_task_input_t *)pvParameters;
	ESP_LOGI(TAG, "Start task_parameter=%s", task_parameter->command);

	// SSH Staff
	int sock;
	struct sockaddr_in sin;
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;

	// Init lib ssh
	esp_err_t ret = initialize_libssh();
	if(ret!=ESP_OK){
		ESP_LOGI(TAG, "Failed to init libssh!");
		ssh_task_fail(task_parameter);
	}

	// Create the socket
	sock = create_socket(&sin); 
	if (sock < 0){
		ESP_LOGE(TAG, "failed to create socket!");
		ssh_task_fail(task_parameter);
	}

	// Connect to socket
	ESP_LOGI(TAG,"Connecting to socket");
	if(connect(sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0) {
		ESP_LOGE(TAG, "failed to connect!");
		ssh_task_fail(task_parameter);
	}

	// Create the ssh session
	session = create_ssh_session(sock);
	if(session == NULL){
		ESP_LOGE(TAG,"Failed to create ssh session!");
		ssh_task_fail(task_parameter);
	}

	// Print the command we're gonna run
	print_command(task_parameter->command);

	// Open SSH channel and exec command
	channel = exec_ssh_command(session, sock, task_parameter->command);
	if(channel == NULL ){
		ESP_LOGE(TAG,"Failed to exec ssh command!");
		ssh_task_fail(task_parameter);
	}

	// Read back response
    const size_t buffer_size = 4096;
    char * rx_buff = malloc(buffer_size*sizeof(char));
    int bytecount = read_channel(channel, session, sock, rx_buff, buffer_size);
	// int bytecount = read_channel(channel, session, sock);
	if(bytecount < 0 ){
		ESP_LOGE(TAG,"Failed to read channel!");
		ssh_task_fail(task_parameter);
	}
    ESP_LOGI(TAG,"SSH CHannel read:\r\n%s",rx_buff);
    free(rx_buff);

	// Close ssh channel
	int rc = close_ssh_channel(channel, session, sock, bytecount);
	if(rc<0){
		ESP_LOGE(TAG,"Failed to close ssh channel!");
		ssh_task_fail(task_parameter);
	}

	// Mark success if we got here
	xEventGroupSetBits( task_parameter->xEventGroup, SSH_CMD_SUCCESS_BIT );

	// Now cleanup
	disconnect_and_cleanup(session,sock);

	// Complete!
	ESP_LOGI(TAG, "[%s] done", task_parameter->command);
	ssh_task_exit(task_parameter);
}

void ssh_shell_session(void *pvParameters)
{
    // null input check
    if(pvParameters==NULL){
        ESP_LOGE(TAG,"Null param!");
	    vTaskDelete( NULL );
    }

    // Get input
	ssh_task_input_t * task_parameter = (ssh_task_input_t *)pvParameters;
	ESP_LOGI(TAG, "Start task_parameter=%s", task_parameter->command);

	// SSH Staff
	int sock;
	struct sockaddr_in sin;
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;

	// Init lib ssh
	esp_err_t ret = initialize_libssh();
	if(ret!=ESP_OK){
		ESP_LOGI(TAG, "Failed to init libssh!");
		ssh_task_fail(task_parameter);
	}

	// Create the socket
	sock = create_socket(&sin); 
	if (sock < 0){
		ESP_LOGE(TAG, "failed to create socket!");
		ssh_task_fail(task_parameter);
	}

	// Connect to socket
	ESP_LOGI(TAG,"Connecting to socket");
	if(connect(sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0) {
		ESP_LOGE(TAG, "failed to connect!");
		ssh_task_fail(task_parameter);
	}

	// Create the ssh session
	session = create_ssh_session(sock);
	if(session == NULL){
		ESP_LOGE(TAG,"Failed to create ssh session!");
		ssh_task_fail(task_parameter);
	}

	// Init RX buff
    const size_t buffer_size = 4096;
	char * tx_buff = malloc(buffer_size*sizeof(char));
    char * rx_buff = malloc(buffer_size*sizeof(char));

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    // Configure UART parameters
    uart_param_config(UART_NUM, &uart_config);

    // Set UART pins
    uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Install UART driver using an event queue here we use the default queue
    uart_driver_install(UART_NUM, buffer_size, buffer_size, 128, NULL, 0);
    
	int index = 0;
	int byte_count = 0;
	ESP_LOGI(TAG,"Now starting shell!");
	while(1){

		int len = uart_read_bytes(UART_NUM, (uint8_t*)rx_buff + index, 1, pdMS_TO_TICKS(20) );
		if (len > 0) {
			uart_write_bytes(UART_NUM, (uint8_t*)rx_buff + index, 1);
			if (rx_buff[index] == '\n' || rx_buff[index] == '\r') {
				rx_buff[index] = '\0'; // Null-terminate the string
				uart_write_bytes(UART_NUM, (uint8_t*)(&"\r\n"), 2);
				ESP_LOG_BUFFER_HEX(TAG,rx_buff,index);
				byte_count = index;
				index = 0; // Reset the buffer index
			} else {
				index++;
				if (index >= buffer_size) {
					// Buffer overflow, reset index
					index = 0;
				}
			}
		}

		// Execute command if byte count is set
        if ( byte_count > 0 ) {

			// Print the char we're gonna send
			print_command(rx_buff);
			
			// Open SSH channel and exec command
			channel = exec_ssh_command(session, sock, rx_buff);
			if(channel == NULL ){
				ESP_LOGE(TAG,"Failed to exec ssh command!");
				ssh_task_fail(task_parameter);
			}

			// Read back response TODO: Make this take a buffer
			int bytecount = read_channel(channel, session, sock, rx_buff, buffer_size);
			if(bytecount < 0 ){
				ESP_LOGE(TAG,"Failed to read channel!");
				ssh_task_fail(task_parameter);
			}
			ESP_LOGI(TAG,"SSH CHannel read:\r\n%s",rx_buff);

			// Clear buffers
			memset(tx_buff,0,buffer_size);
			memset(rx_buff,0,buffer_size);
			byte_count = 0;
		}
	}

	// Release buffers
    free(tx_buff);
    free(rx_buff);

	// Close ssh channel
	int rc = close_ssh_channel(channel, session, sock, 0);
	if(rc<0){
		ESP_LOGE(TAG,"Failed to close ssh channel!");
		ssh_task_fail(task_parameter);
	}

	// Mark success if we got here
	xEventGroupSetBits( task_parameter->xEventGroup, SSH_CMD_SUCCESS_BIT );

	// Now cleanup
	disconnect_and_cleanup(session,sock);

	// Complete!
	ESP_LOGI(TAG, "[%s] done", task_parameter->command);
	ssh_task_exit(task_parameter);
}

esp_err_t create_ssh_task_input(ssh_task_input_t * task_parameters, char * command)
{
	// Create Eventgroup
	EventGroupHandle_t xEventGroup = xEventGroupCreate();
	configASSERT( xEventGroup );
	xEventGroupClearBits( xEventGroup, SSH_TASK_FINISH_BIT );
	
	ssh_task_input_t input = {
		.xEventGroup = xEventGroup,
		.command = command,
	};
	*task_parameters = input;

    return ESP_OK;
}

esp_err_t delete_ssh_task_input(ssh_task_input_t * task_parameters)
{
	// De-alloc event group
	vEventGroupDelete(task_parameters->xEventGroup);
	return ESP_OK;
}

esp_err_t run_ssh_task_blocked(char * command)
{
    
    esp_err_t ret = ESP_OK;

    // Create input for ssh task
    ssh_task_input_t task_parameters;
    ESP_ERROR_CHECK( create_ssh_task_input((ssh_task_input_t *)&task_parameters, (char *)command));

    // Execute ssh command
    xTaskCreate(&ssh_task, "SSH", 1024 * 8, (void *)&task_parameters, 2, NULL);

    // Wait for ssh task to finish.
    xEventGroupWaitBits(task_parameters.xEventGroup,
                        SSH_TASK_FINISH_BIT, /* The bits within the event group to wait for. */
                        pdTRUE,              /* SSH_TASK_FINISH_BIT should be cleared before returning. */
                        pdFALSE,             /* Don't wait for both bits, either bit will do. */
                        portMAX_DELAY);      /* Wait forever. */

    // Check if failed
    EventBits_t flags = xEventGroupGetBits(task_parameters.xEventGroup);
    if (flags & SSH_TASK_FAIL_BIT) {
        ESP_LOGE(TAG, "SSH task failed!");
        ret = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "SSH task finished successfully!");
        ret = ESP_OK;
    }

    // Delete the input
    ESP_ERROR_CHECK(delete_ssh_task_input((ssh_task_input_t *)&task_parameters));

    return ret;
}

esp_err_t run_ssh_shell_session_blocked(void)
{
    
    esp_err_t ret = ESP_OK;

    // Create input for ssh task
    ssh_task_input_t task_parameters;
    ESP_ERROR_CHECK( create_ssh_task_input((ssh_task_input_t *)&task_parameters, (char *)"dummy"));

    // Execute ssh command
    xTaskCreate(&ssh_shell_session, "SSH-SHELL", 1024 * 8, (void *)&task_parameters, 2, NULL);

    // Wait for ssh task to finish.
    xEventGroupWaitBits(task_parameters.xEventGroup,
                        SSH_TASK_FINISH_BIT, /* The bits within the event group to wait for. */
                        pdTRUE,              /* SSH_TASK_FINISH_BIT should be cleared before returning. */
                        pdFALSE,             /* Don't wait for both bits, either bit will do. */
                        portMAX_DELAY);      /* Wait forever. */

    // Check if failed
    EventBits_t flags = xEventGroupGetBits(task_parameters.xEventGroup);
    if (flags & SSH_TASK_FAIL_BIT) {
        ESP_LOGE(TAG, "SSH task failed!");
        ret = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "SSH task finished successfully!");
        ret = ESP_OK;
    }

    // Delete the input
    ESP_ERROR_CHECK(delete_ssh_task_input((ssh_task_input_t *)&task_parameters));

    return ret;
}

static void ssh_task_exit(ssh_task_input_t * task_parameter)
{
	xEventGroupSetBits( task_parameter->xEventGroup, SSH_TASK_FINISH_BIT );
	
	// // Dump gcov data
	// printf("Ready to dump GCOV data...\n");
	// esp_gcov_dump();
	// printf("GCOV data have been dumped.\n");
	
	vTaskDelete( NULL );
}

static void ssh_task_fail(ssh_task_input_t * task_parameter)
{
	xEventGroupSetBits( task_parameter->xEventGroup, SSH_TASK_FAIL_BIT );
	ssh_task_exit(task_parameter);
}
