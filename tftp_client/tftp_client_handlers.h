#ifndef TFTP_CLIENT_HANDLERS_H
#define TFTP_CLIENT_HANDLERS_H


#include "../utils/tftp_utils.h"
#include "../utils/platform_exec.h"




/*
    ports functions,
    one is a getter of available ports,
    one is a setter for client port,
    one is a releaser
*/
int get_next_port();


void set_client_port(struct sockaddr_in *client_addr);


void release_port(int port);

//oper handlers
void rrq_h(int sockfd, struct sockaddr_in *server_addr, char *filename, const char *mode);
void wrq_h(int sockfd, struct sockaddr_in *server_addr, char *filename, const char *mode);
void del_h(int sockfd, struct sockaddr_in *server_addr);


#endif
