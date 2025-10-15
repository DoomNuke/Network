#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#define _POSIX_C_SOURCE 200809L
#include <signal.h>

#include "tftp_server.h"

volatile sig_atomic_t server_running = 1; // calling the control+c handler

void sigint_server(int sig)
{
    (void)sig; // To not use the argument
    printf("Received Control+C, exitting...\n");
    server_running = 0;
}

void setup_signal_handler(void)
{
    struct sigaction sa;
    sa.sa_handler = sigint_server;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART recvfrom will be interrupted

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

int main()
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[TFTP_BUF_SIZE];
    ssize_t recv_len;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Error creating socket\n");
        exit(EXIT_FAILURE);
    }

    if (!dir_exist(TFTP_ROOT_DIR))
    {
        fprintf(stderr, "Failed to ensure client directory exists. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // listening to all interfaces
    server_addr.sin_port = htons(TFTP_PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error binding socket");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("TFTP server has started listening to requests\n");
    logger("INFO", "Server has started\n");

    if (signal(SIGINT, sigint_server) == SIG_ERR)
    {
        perror("Error setting signal handler");
        exit(EXIT_FAILURE);
    }

    setup_signal_handler(); // for signal handler

    while (server_running)
    {
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
        if (recv_len < 0)
        {
            if (!server_running)
            {
                break; // exit loop
            }
            printf("Waiting for requests from the client...\n");
            continue;
        }

        printf("Received packet from client\n");

        // Extract the opcode (first 2 bytes)
        uint16_t opcode = (buffer[0] << 8) | buffer[1];

        // Extract the filename and mode (assuming they are after the opcode)
        const char *filename = (const char *)(buffer + 2);

        // Find where the filename ends and the mode starts
        const char *mode_start = strchr(filename, 0) + 1; // Find null byte marking the end of filename
        const char *mode = mode_start;

        // Handle the different opcodes
        switch (opcode)
        {
        case TFTP_OPCODE_RRQ: // Read Request
            printf("Received RRQ (Read Request) from client\n");
            rrq_handler(sockfd, &client_addr, client_len, filename, mode);
            break;

        case TFTP_OPCODE_WRQ: // Write Request
            printf("Received WRQ (Write Request) from client\n");
            wrq_handler(sockfd, &client_addr, client_len, filename, mode);
            break;

        case TFTP_OPCODE_DEL: // Delete Request
            printf("Received DEL (Delete Request) from client\n");
            del_handler(sockfd, &client_addr, client_len, filename);
            break;

        default:
            logger("ERROR", "Unknown opcode received: %d\n", opcode);
            break;
        }
    }

    close(sockfd);
    logger("INFO", "Server has shut down\n");
}