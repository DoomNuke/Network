#ifndef TFTP_SERVER_HANDLERS_H
#define TFTP_SERVER_HANDLERS_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../common/tftp_common.h"


//File writing based on transfer mode
ssize_t write_file_data(FILE *file, const char *buffer, size_t size, const char *mode);

//ACK,WRQ,PARSE_WRQ,RRQ,DEL handlers
void send_ack(int sockfd, struct sockaddr_in *client_addr, tftp_packet_t *packet);
void wrq_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode);
int parse_wrq_packet(const char *buffer, char *filename, char *mode);
void rrq_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode);
void del_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode);


#endif 