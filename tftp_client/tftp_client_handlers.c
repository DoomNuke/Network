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

// for stabling multi-threading
int ports[MAX_PORTS] = {6970, 6971, 6972, 6973, 6974, 6975, 6976, 6977, 6978, 6979};
int used[MAX_PORTS] = {0};

int get_next_port()
{
    int available_ports[MAX_PORTS];
    int count = 0;

    for (int i = 0; i < MAX_PORTS; i++)
    {
        if (!used[i])
        {
            available_ports[count++] = i;
        }
    }
    if (count == 0)
    {
        return -1; // no ports available
    }
    // pick random index from available ports
    int rand_index = available_ports[rand() % count];
    used[rand_index] = 1;
    return ports[rand_index];
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

void release_port(int port)
{
    for (int i = 0; i < MAX_PORTS; i++)
    {
        if (ports[i] == port)
        {                // find the port in the ports array
            used[i] = 0; // port available if value = 1 used
            printf("Port %d released\n", port);
            return; // return when released the port
        }
    }
    printf("Port %d not found in used list\n", port);
}

int send_data(int sockfd, struct sockaddr_in *server_addr, FILE *file)
{
    char buffer[TFTP_BUF_SIZE];
    uint16_t block_n = 1;
    size_t bytes_read;
    int retries;
    char ack_buf[4];
    socklen_t addr_len = sizeof(*server_addr);

    while (1)
    {
        retries = 0;

        // opcode set
        buffer[0] = 0;
        buffer[1] = TFTP_OPCODE_DATA;
        buffer[2] = (block_n >> 8) & 0xFF;
        buffer[3] = block_n & 0xFF;

        bytes_read = fread(buffer + 4, 1, TFTP_DATA_SIZE, file);
        if (ferror(file))
        {
            perror("File read error");
            return -1;
        }

        size_t sent = sendto(sockfd, buffer, 4 + bytes_read, 0,
                             (struct sockaddr *)server_addr, addr_len);
        if (sent < 0)
        {
            perror("Sending file contents failed");
            return -1;
        }

        while (retries < MAX_RETRIES)
        {

            ssize_t received = recvfrom(sockfd, ack_buf, sizeof(ack_buf), 0,
                                        (struct sockaddr *)server_addr, &addr_len);

            if (received >= 4 && ack_buf[1] == TFTP_OPCODE_ACK)
            {
                uint16_t ack_block = (ack_buf[2] << 8) | ack_buf[3];
                if (ack_block == block_n)
                {
                    printf("ACK Received for block number: %d\n", block_n);
                    break; // ACK Received
                }
                else
                {
                    fprintf(stderr, "Unexpected ACK block number: %d\n", ack_block);
                }
            }
            else
            {
                perror("ACK not received or invalid");
            }

            sendto(sockfd, buffer, 4 + bytes_read, 0,
                   (struct sockaddr *)server_addr, addr_len);
            retries++;
            sleep(2);
        }

        if (retries == MAX_RETRIES)
        {
            fprintf(stderr, "Failed to receive ACK after %d retries, aborting\n", MAX_RETRIES);
            return -1;
        }

        // last block send check
        if (bytes_read < TFTP_DATA_SIZE)
        {
            break;
        }

        block_n++;
    }

    return 0; // success
}

// WRQ client handler
void wrq_h(int sockfd, struct sockaddr_in *server_addr, char *filename, const char *mode)
{
    socklen_t addr_len = sizeof(*server_addr);
    char filepath[PATH_LENGTH];

    printf("Do you want to create a new file (y/n)? ");
    char choice;
    scanf(" %c", &choice);
    getchar(); // clear newline

    if (choice == 'y' || choice == 'Y')
    {
        printf("Enter the name of the new file: ");
        scanf("%s", filename);
        getchar();
    }
    else
    {
        printf("Enter the filename to upload: ");
        scanf("%s", filename);
        getchar();
    }

    snprintf(filepath, sizeof(filepath), "tftp_root/%s", filename);

    // wrq packet prepartion
    char buffer[TFTP_BUF_SIZE];
    memset(buffer, 0, sizeof(buffer)); // clearing the buffer
    uint16_t opcode = htons(TFTP_OPCODE_WRQ);
    memcpy(buffer, &opcode, 2);
    strcpy(buffer + 2, filename);
    strcpy(buffer + 2 + strlen(filename) + 1, mode);
    size_t wrq_len = 2 + strlen(filename) + 1 + strlen(mode) + 1;

    // retrying to get ack(0)
    char ack_buf[4];
    int retries = 0;
    int ack_received = 0;

    while (retries < MAX_RETRIES)
    {
        ssize_t bytes_sent = sendto(sockfd, buffer, wrq_len, 0,
                                    (struct sockaddr *)server_addr, addr_len);
        if (bytes_sent < 0)
        {
            perror("Error sending WRQ");
            return;
        }

        printf("Sent WRQ attempt %d for file '%s' in '%s' mode\n", retries + 1, filename, mode);

        ssize_t received = recvfrom(sockfd, ack_buf, sizeof(ack_buf), 0,
                                    (struct sockaddr *)server_addr, &addr_len);
        if (received >= 4 && ack_buf[1] == TFTP_OPCODE_ACK)
        {
            uint16_t ack_block = ((uint16_t)ack_buf[2] << 8) | (uint16_t)ack_buf[3];
            if (ack_block == 0)
            {
                printf("Received ACK(0), ready to send data\n");
                ack_received = 1;
                break;
            }
        }

        retries++;
        printf("Retrying to receive ACK(0)... attempt %d\n", retries);
        sleep(5);
    }

    if (!ack_received)
    {
        fprintf(stderr, "Timed out waiting for ACK(0)\n");
        return;
    }

    // letting the user create his own file
    if (choice == 'y' || choice == 'Y')
    {
        FILE *file = fopen(filepath, "w");
        if (!file)
        {
            perror("Error creating file");
            return;
        }

        printf("Enter content for the file (Ctrl+D to end input):\n");
        char line[256];
        while (fgets(line, sizeof(line), stdin))
        {
            write_netascii(file, line, strlen(line));
        }
        if (feof(stdin))
        {
            clearerr(stdin);
            fclose(file);
            printf("File '%s' created\n", filepath);
            printf("\e[1;1H\e[2J"); // clear screen after echo of the text, ANSI escape code
        }
    }
    else if (choice == 'n' || choice == 'N')
    {
        snprintf(filepath, sizeof(filepath), "tftp_client_folder/%s", filename);

        /*
        To check whether it's an octet or netascii
        via the file extension
        */
        const char *mode = get_mode(filename);

        if (str_casecmp(mode, "netascii") == 0)
        {
            FILE *test = fopen(filepath, "r");
            if (!test)
            {
                perror("Error reading file");
                return;
            }
            fclose(test);
        }
        else if ((str_casecmp(mode, "octet") == 0))
        {
            FILE *test = fopen(filepath, "rb");
            if (!test)
            {
                perror("Error reading file");
                return;
            }
            fclose(test);
        }
    }
    else
    {
        printf("Invalid input\n");
        return;
    }

    // open file for sending
    FILE *data_to_send = fopen(filepath, "rb");
    if (!data_to_send)
    {
        perror("Failed to open file for sending");
        return;
    }

    // send data
    int ret = send_data(sockfd, server_addr, data_to_send);
    fclose(data_to_send);

    if (ret == 0)
    {
        printf("File sent successfully.\n");
    }
    else
    {
        fprintf(stderr, "Error during file sending.\n");
        return;
    }
}

// Function to handle RRQ (Read Request)
void rrq_h(int sockfd, struct sockaddr_in *server_addr, char *filename)
{
    char mode[16];
    socklen_t addr_len = sizeof(*server_addr);
    char buffer[TFTP_BUF_SIZE];
    FILE *file = NULL; // open file only after first data block arrives

    printf("Enter the filename to print/execute (netascii/octet): ");
    if (scanf("%255s", filename) != 1)
    {
        fprintf(stderr, "Error reading filename\n");
        return;
    }

    // automatic determination of the file extension aka mode
    strncpy(mode, get_mode(filename), sizeof(mode) - 1);
    mode[sizeof(mode) - 1] = '\0'; // for null termination

    // Clear stdin buffer
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF)
        ;

    // Prepare RRQ packet
    memset(buffer, 0, sizeof(buffer)); // clearing the buffer
    uint16_t opcode = htons(TFTP_OPCODE_RRQ);
    memcpy(buffer, &opcode, sizeof(opcode));
    strcpy(buffer + 2, filename);
    strcpy(buffer + 2 + strlen(filename) + 1, mode);

    ssize_t bytes_sent = sendto(sockfd, buffer, 2 + strlen(filename) + 1 + strlen(mode) + 1, 0,
                                (struct sockaddr *)server_addr, addr_len);
    if (bytes_sent < 0)
    {
        perror("Error sending RRQ");
        return;
    }

    printf("Sent RRQ for file '%s' in '%s' mode\n", filename, mode);

    // Set socket receive timeout (5 seconds)
    struct timeval tv = {5, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt failed");
        // continue anyways, not fatal
    }

    char full_path[PATH_LENGTH];
    snprintf(full_path, sizeof(full_path), "%s/%s", TFTP_CLIENT_DIR, filename);

    uint16_t expected_block = 1;
    uint16_t last_ack_block = 0;

    file = fopen(full_path, "wb");
    {
        if (!file)
        {
            perror("Error opening file to write");
            return;
        }
    };

    // TODO: fix it might be a problem with the server too
    while (1)
    {
        socklen_t src_len = sizeof(*server_addr);
        ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                    (struct sockaddr *)server_addr, &src_len);
        if (recv_len < 0)
        {
            perror("recvfrom failed or timed out");
            break;
        }
        if (recv_len < 4)
        {
            fprintf(stderr, "Invalid packet received\n");
            continue;
        }

        uint16_t recv_opcode = (buffer[0] << 8) | buffer[1];
        if (recv_opcode != TFTP_OPCODE_DATA)
        {
            fprintf(stderr, "Unexpected packet opcode: %d\n", recv_opcode);
            break;
        }

        uint16_t block_num = (buffer[2] << 8) | buffer[3];
        printf("Received DATA block %d, %zd bytes\n", block_num, recv_len - 4);

        if (block_num == expected_block)
        {
            printf("MATCHING BLOCK NUM\n");
            // Open file on first data block received

            // Write data payload
            fwrite(buffer + 4, 1, recv_len - 4, file);

            // Send ACK
            char ack_pkt[4] = {0, TFTP_OPCODE_ACK,
                               (block_num >> 8) & 0xFF,
                               block_num & 0xFF};
            ssize_t sent = sendto(sockfd, ack_pkt, 4, 0, (struct sockaddr *)server_addr, src_len);

            if (sent < 0)
            {
                perror("Failed to send ACK");
                break;
            }
            printf("Sent ACK for block %d\n", block_num);

            last_ack_block = block_num;
            expected_block++;

            printf("sent all blocks %d", last_ack_block);

            // check if last block of data
            if (recv_len < 4 + TFTP_DATA_SIZE)
                break;
        }
        else
        {
            fprintf(stderr, "Received unexpected block %d, no ACK resent\n", block_num);
        }
    }

    if (file)
        fclose(file);

    // print or execute
    handle_user_action(filename, mode, TFTP_CLIENT_DIR);
}

// Function to handle DEL (Delete Request)
void del_h(int sockfd, struct sockaddr_in *server_addr)
{
    char filename[PATH_LENGTH];
    printf("Enter the filename you want to delete: ");
    scanf("%s", filename);

    socklen_t addr_len = sizeof(*server_addr);

    // Build DELETE request
    char buffer[TFTP_BUF_SIZE];
    memset(buffer, 0, sizeof(buffer));

    buffer[0] = 0;
    buffer[1] = TFTP_OPCODE_DEL; // Use raw opcode byte
    strcpy(buffer + 2, filename);

    // Send the DEL packet
    ssize_t bytes_sent = sendto(sockfd, buffer, 2 + strlen(filename) + 1, 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    if (bytes_sent < 0)
    {
        perror("Error sending DEL request");
        return;
    }

    printf("Sent DEL request for file '%s'\n", filename);

    // Wait for server's ACK or ERROR
    ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, &addr_len);

    if (recv_len < 0)
    {
        perror("Failed to receive response from server");
        return;
    }

    if (buffer[0] == 0 && buffer[1] == TFTP_OPCODE_ACK)
    {
        uint16_t block_n = (buffer[2] << 8) | buffer[3];
        if (block_n == 0)
        {
            printf("Server confirmed file '%s' deleted successfully.\n", filename);
        }
        else
        {
            printf("Unexpected ACK block number: %d\n", block_n);
        }
    }
    else if (buffer[0] == 0 && buffer[1] == TFTP_OPCODE_ERROR)
    {
        uint16_t error_code = (buffer[2] << 8) | buffer[3];
        printf("Server responded with ERROR %d: %s\n", error_code, buffer + 4);
    }
    else
    {
        printf("Unexpected response from server.\n");
    }
}
