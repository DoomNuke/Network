#ifndef TFTP_CLIENT_HANDLERS_H
#define TFTP_CLIENT_HANDLERS_H


#include "../utils/tftp_utils.h"
#include "../utils/platform_exec.h"



//catching available ports
int get_next_port();

//setting the caught port
void set_client_port(struct sockaddr_in *client_addr);

//release the port
void release_port(int port);

//to send ack
void send_ack(int sockfd, struct sockaddr_in *server_addr, uint16_t block_n);

//oper handlers
void rrq_h(int sockfd, struct sockaddr_in *server_addr, const char *filename, const char *mode);
void wrq_h(int sockfd, struct sockaddr_in *server_addr, char *filename, const char *mode);
void del_h(int sockfd, struct sockaddr_in *server_addr);


#endif
