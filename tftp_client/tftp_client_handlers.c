#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <poll.h>
#include <errno.h>

#include "tftp_client_handlers.h"
#include "tftp_client.h"


//for stabling multi-threading 
int ports[MAX_PORTS] = {6970, 6971, 6972, 6973, 6974, 6975, 6976, 6977, 6978,6979};
int used[MAX_PORTS] = {0}; 

int get_next_port()
{
    for (int i = 0; i < MAX_PORTS; i++) {
        if (!used[i]) {
            used[i] = 1;  // Mark port as used
            return ports[i];
        }
    }
    return -1;  // No available ports
}

void set_client_port(struct sockaddr_in *client_addr)
{
    int port = get_next_port();
    if (port == -1)
    {
        printf("No available ports\n");
        exit(EXIT_FAILURE);
    }
    client_addr->sin_port = htons(port);
    printf("Client using port %d\n", port);
}

void release_port(int port) {
    for (int i = 0; i < MAX_PORTS; i++) {
        if (ports[i] == port) {  //find the port in the ports array
            used[i] = 0;  //port available if value = 1 used
            printf("Port %d released\n", port);
            return;  //return when released the port
        }
    }
    printf("Port %d not found in used list\n", port);
}




void send_ack(int sockfd, struct sockaddr_in *server_addr, uint16_t block_n) {
    char ack_packet[4];
    ack_packet[0] = 0;  // Opcode: ACK (0x04)
    ack_packet[1] = TFTP_OPCODE_ACK;
    ack_packet[2] = (block_n >> 8) & 0xFF;  // Most significant byte of block number
    ack_packet[3] = block_n & 0xFF;         // Least significant byte of block number

    ssize_t sent = sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    if (sent < 0) {
        perror("Error sending ACK");
    } else {
        printf("Sent ACK for block number %d\n", block_n);
    }
}

// Function to handle WRQ (Write Request)
void wrq_h(int sockfd, struct sockaddr_in *server_addr, char *filename, const char *mode) 
{
    printf("Do you want to create a new file (y/n)? ");
    char choice;
    scanf(" %c", &choice);

    if (choice == 'y' || choice == 'Y') {
        // If user wants to create a new file
        printf("Enter the name of the new file: ");
        scanf("%s", filename);

        // Write content to the new file (netascii mode)
        FILE *file = fopen(filename, "w");
        if (!file) {
            perror("Error creating file");
            return;
        }
        printf("Enter content for the file (Ctrl+D to end input):\n");
        char line[256];
        while (fgets(line, sizeof(line), stdin)) {
            write_netascii(file, line, strlen(line));
        }
        fclose(file);
        printf("File '%s' created and saved to tftp_root.\n", filename);
    } else {
        // If user wants to upload an existing file
        printf("Enter the filename to upload: ");
        scanf("%s", filename);
    }

    // Create the WRQ packet
    char buffer[TFTP_BUF_SIZE];
    memset(buffer, 0, sizeof(buffer));

    uint16_t opcode = htons(TFTP_OPCODE_WRQ);  // Write Request opcode
    memcpy(buffer, &opcode, sizeof(opcode));   // Copy opcode into buffer
    strcpy(buffer + 2, filename);              // Copy filename into buffer
    strcpy(buffer + 2 + strlen(filename) + 1, mode); // Copy mode into buffer

    // Send the WRQ packet to the server
    ssize_t bytes_sent = sendto(sockfd, buffer, 4 + strlen(filename) + strlen(mode), 0,
                                (struct sockaddr *)server_addr, sizeof(*server_addr));
    if (bytes_sent < 0) {
        perror("Error sending WRQ");
    } else {
        printf("Sent WRQ for file '%s' in '%s' mode\n", filename, mode);
    }


    send_ack(sockfd, server_addr, 0);
}

// Function to handle RRQ (Read Request)
void rrq_h(int sockfd, struct sockaddr_in *server_addr, const char *filename, const char *mode) 
{
    // Create the RRQ packet
    char buffer[TFTP_BUF_SIZE];
    memset(buffer, 0, sizeof(buffer));

    uint16_t opcode = htons(TFTP_OPCODE_RRQ);  // Read Request opcode
    memcpy(buffer, &opcode, sizeof(opcode));   // Copy opcode into buffer
    strcpy(buffer + 2, filename);              // Copy filename into buffer
    strcpy(buffer + 2 + strlen(filename) + 1, mode); // Copy mode into buffer

    // Send the RRQ packet to the server
    ssize_t bytes_sent = sendto(sockfd, buffer, 4 + strlen(filename) + strlen(mode), 0,
                                (struct sockaddr *)server_addr, sizeof(*server_addr));
    if (bytes_sent < 0) {
        perror("Error sending RRQ");
    } else {
        printf("Sent RRQ for file '%s' in '%s' mode\n", filename, mode);
    }


    send_ack(sockfd, server_addr, 0);

    handle_user_action(filename, mode); //checks if the user wants to print the file (if it's netascii) or execute it
}

// Function to handle DEL (Delete Request)
void del_h(int sockfd, struct sockaddr_in *server_addr) {
    char filename[PATH_LENGTH];
    printf("Enter the filename you want to delete: ");
    scanf("%s", filename);

    // Create the DEL packet
    char buffer[TFTP_BUF_SIZE];
    memset(buffer, 0, sizeof(buffer));

    uint16_t opcode = htons(TFTP_OPCODE_DEL);  // Delete Request opcode
    memcpy(buffer, &opcode, sizeof(opcode));   // Copy opcode into buffer
    strcpy(buffer + 2, filename);              // Copy filename into buffer

    // Send the DEL packet to the server
    ssize_t bytes_sent = sendto(sockfd, buffer, 2 + strlen(filename), 0,
                                (struct sockaddr *)server_addr, sizeof(*server_addr));
    if (bytes_sent < 0) {
        perror("Error sending DEL");
    } else {
        printf("Sent DEL request for file '%s'\n", filename);
    }


    send_ack(sockfd, server_addr, 0);
}

