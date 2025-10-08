#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "tftp_client.h"

volatile sig_atomic_t client_running = 1; // control+c handler for client

// for control+c
void sigint_client(int sig)
{
    (void)sig; // To not use the argument
    printf("Received Control+C, exitting...\n");
    client_running = 0;
}

void setup_signal_handler(void)
{
    struct sigaction sa;
    sa.sa_handler = sigint_client;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART recvfrom will be interrupted

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

// helper function
int is_text_file(const char *filename)
{
    if (filename == NULL)
    {
        return 0;
    }
    return 1;
}

// Main function for the TFTP client
int main()
{
    const char *client_ip = "127.0.0.1"; // loopback, same goes for server
    char ip_add[60];
    char filename[256];

    if (!dir_exist(TFTP_CLIENT_DIR))
    {
        fprintf(stderr, "Failed to ensure client directory exists. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(ip_add, sizeof(ip_add), "Client IP is: %s", client_ip);
    printf("%s\n", ip_add);

    const char *mode = is_text_file(filename) ? "netascii" : "octet";

    int sockfd;
    struct sockaddr_in client_addr;
    struct sockaddr_in server_addr;

    // Create the socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    srand(time(NULL)); // for randomizing each port

    // client address setup
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    set_client_port(&client_addr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TFTP_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (bind(sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
    {
        perror("Client bind failed");
        release_port(ntohs(client_addr.sin_port)); // if bind fails, just to make sure
        exit(EXIT_FAILURE);
    }

    // telling which port are being used
    printf("Client address set with port %d\n", ntohs(client_addr.sin_port));

    // setting up signal for sigint
    setup_signal_handler();

    char input[10];
    int choice;


    while (client_running)
    {

        printf("Would you like to:\n");
        printf("1. Request a file (RRQ)\n");
        printf("2. Upload a file (WRQ)\n");
        printf("3. Delete a file (DEL)\n");
        printf("4. Exit the program\n");
        printf("Enter choice: ");


        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            printf("Error reading input\n");
            continue; // or handle exit
        }

        // Remove newline if present
        input[strcspn(input, "\n")] = 0;

        // Parse input as integer
        if (sscanf(input, "%d", &choice) != 1)
        {
            printf("Invalid choice, try again.\n");
            continue;
        }

        switch (choice)
        {
        case 1:
            rrq_h(sockfd, &server_addr, filename);
            break;
        case 2:
            wrq_h(sockfd, &server_addr, filename, mode);
            break;
        case 3:
            del_h(sockfd, &server_addr);
            break;
        case 4:
            printf("Exiting and freeing port... %d\n", ntohs(client_addr.sin_port));
            client_running = 0; // STOP the loop
            break;
        default:
            printf("Invalid choice, try again.\n");
            break;
        }
    }
    // Close the socket and release port
    release_port(ntohs(client_addr.sin_port));

    close(sockfd);
    return 0;
}
