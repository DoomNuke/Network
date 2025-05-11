#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "tftp_client.h"

extern volatile sig_atomic_t client_running; //control+c handler for client



// Main function for the TFTP client
int main() {
    const char *client_ip = "127.0.0.1";
    char ip_add[60];
    char filename[256];
    char user_file[128];
    int is_text_file(const char *filename);


    snprintf(ip_add, sizeof(ip_add), "Client IP is: %s", client_ip);
    snprintf(filename, sizeof(filename), "tftp_client/%s", user_file);

    const char *mode = is_text_file(filename) ? "netascii" : "octet";

    int sockfd;
    struct sockaddr_in client_addr;

    // Create the socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    //client address setup
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY; 
    set_client_port(&client_addr);

    if (bind(sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Client bind failed");
        exit(EXIT_FAILURE);
    }

    //telling which port are being used
    printf("Client address set with port %d\n", ntohs(client_addr.sin_port));

    // Ask user whether to perform RRQ, WRQ, or DEL
    printf("Would you like to:\n");
    printf("1. Request a file (RRQ)\n");
    printf("2. Upload a file (WRQ)\n");
    printf("3. Delete a file (DEL)\n");
    int choice;
    scanf("%d", &choice);


    if (choice == 1) {
        //rrq req 
       rrq_h(sockfd, &client_addr, filename, mode);
    } else if (choice == 2) {
        //wrq req
        wrq_h(sockfd, &client_addr, filename, mode);
    } else if (choice == 3) {
        //delete req
        del_h(sockfd, &client_addr);
    }

    // Close the socket and release port
    release_port(ntohs(client_addr.sin_port));

    close(sockfd);
    return 0;
}