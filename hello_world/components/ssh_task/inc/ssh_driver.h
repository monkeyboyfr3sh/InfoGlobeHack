#ifndef _SSH_DRIVER_H
#define _SSH_DRIVER_H


#include "libssh2_config.h"
#include <libssh2.h>
#include <libssh2_sftp.h>
#include "esp_err.h"

#include <sys/socket.h>
#include "libssh2_config.h"
#include <libssh2.h>
#include <libssh2_sftp.h>


void print_command(char* command);
esp_err_t initialize_libssh( void );
int create_socket(struct sockaddr_in *sin);
LIBSSH2_SESSION *create_ssh_session(int sock);
LIBSSH2_CHANNEL *exec_ssh_command(LIBSSH2_SESSION *session, int sock, char * command);
int read_channel(LIBSSH2_CHANNEL *channel, LIBSSH2_SESSION *session, int sock, char *input_buffer, int buffer_size);
int close_ssh_channel(LIBSSH2_CHANNEL *channel, LIBSSH2_SESSION *session, int sock, int bytecount);
void disconnect_and_cleanup(LIBSSH2_SESSION *session, int sock);

#endif