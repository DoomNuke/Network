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

/*
    ports functions,
    one is a randomizer, 
    one is a setter,
    one is a releaser
*/
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

// WRQ client handler
void wrq_h(int sockfd, struct sockaddr_in *server_addr, char *filename, const char *mode)
{
    socklen_t server_len = sizeof(*server_addr);
    char buffer[TFTP_BUF_SIZE];
    char filepath[PATH_LENGTH];
    int c;
    char answer;
    uint16_t block_n = 1;
    ssize_t sent_len;
    ssize_t recv_len;
    ssize_t bytes_read = 0;
    FILE *file;
    unsigned char ack_buf[4]; // Separate buffer for ACKs

    printf("Do you want to create a new file (y/n)? ");
    scanf(" %c", &answer);

    // letting the user create his own file
    if (answer == 'y' || answer == 'Y')
    {
        printf("Enter the name for the new file:\n");
        scanf("%s", filename);

        snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_CLIENT_DIR, filename);

        /*
        opening it wb because of CRLF conversion
        since im on linux i decided to treat it that way so it gets treated
        byte by byte and it disables automatically the newline conversion
        */
        file = fopen(filepath, "wb");
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
            while ((c = getchar()) != '\n' && c != EOF);
            clearerr(stdin);
            printf("\nFile '%s' created\n", filepath);
        }
    }

    else if (answer == 'n' || answer == 'N')
    {
        printf("Enter the name for the file:\n");
        scanf("%s", filename);
        while ((c = getchar()) != '\n' && c != EOF);

        snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_CLIENT_DIR, filename);

        /*
            To check whether it's an octet or netascii
            via the file extension, automoatic checkup
        */

        file = fopen(filepath, "rb");
        if (!file)
        {
            perror("Error reading file");
            return;
        }

        mode = get_mode(filename);

        if (strcmp(mode, "netascii") && strcmp(mode, "octet") != 0)
        {
            fprintf(stderr, "Unsupported mode %s\n", mode);
            fclose(file);
            return;
        }
    }

    /*
    In case the user didn't pressed y or n
    */
    else
    {
        printf("Error, please enter y/n\n");
        return;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    printf("file size is %ld\n", file_size);

    // wrq packet preperation
    memset(buffer, 0, sizeof(buffer)); // clearing the buffer
    uint16_t opcode = htons(TFTP_OPCODE_WRQ);
    memcpy(buffer, &opcode, sizeof(opcode));
    strcpy(buffer + 2, filename);
    strcpy(buffer + 2 + strlen(filename) + 1, mode);

    printf("WRQ attempt for file '%s' in '%s' mode\n", filename, mode);

    // Set socket receive timeout (5 seconds)
    struct timeval tv = {5, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt failed");
        // continue anyways, not fatal
    }

    sent_len = sendto(sockfd, buffer, 2 + strlen(filename) + 1 + strlen(mode) + 1, 0,
                      (struct sockaddr *)server_addr, server_len);
    if (sent_len < 0)
    {
        perror("Error sending WRQ\n");
        return;
    }

    recv_len = recvfrom(sockfd, ack_buf, sizeof(ack_buf), 0,
                        (struct sockaddr *)server_addr, &server_len);
    if (recv_len < 0)
    {
        perror("Error Receiving ACK\n");
        return;
    }

    do
    {
        // Read the next block of data
        if (str_casecmp(mode, "netascii") == 0)
            bytes_read = read_netascii(file, buffer + 4, TFTP_DATA_SIZE);
        else
            bytes_read = read_octet(file, buffer + 4, TFTP_DATA_SIZE);

        // Build the DATA packet
        buffer[0] = 0;
        buffer[1] = TFTP_OPCODE_DATA;
        buffer[2] = (block_n >> 8) & 0xFF;
        buffer[3] = block_n & 0xFF;

        int retries = 0;

        while (retries < MAX_RETRIES)
        {
            // Send the DATA packet
            sent_len = sendto(sockfd, buffer, bytes_read + 4, 0,
                              (struct sockaddr *)server_addr, server_len);
            if (sent_len < 0)
            {
                perror("sendto failed");
                fclose(file);
                return;
            }

            // Wait for ACK
            recv_len = recvfrom(sockfd, ack_buf, sizeof(ack_buf), 0,
                                (struct sockaddr *)server_addr, &server_len);

            if (recv_len < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // Timeout — retry sending the same data
                    retries++;
                    fprintf(stderr, "Timeout waiting for ACK for block %d. Retrying (%d/%d)...\n",
                            block_n, retries, MAX_RETRIES);
                    continue;
                }
                else
                {
                    perror("recvfrom failed");
                    fclose(file);
                    return;
                }
            }

            // Check if it's a valid ACK
            if (recv_len == 4 && ack_buf[0] == 0 && ack_buf[1] == TFTP_OPCODE_ACK)
            {
                uint16_t ack_block = (ack_buf[2] << 8) | ack_buf[3];
                if (ack_block == block_n)
                {
                    // Valid ACK received
                    break;
                }
                else
                {
                    fprintf(stderr, "Received ACK for unexpected block %d (expected %d). Ignoring...\n", ack_block, block_n);
                }
            }

            // If we get here, either bad ack or wrong block number
            retries++;
        }

        if (retries >= MAX_RETRIES)
        {
            fprintf(stderr, "Max retries reached for block %d. Aborting transfer.\n", block_n);
            fclose(file);
            return;
        }

        // Proceed to next block
        block_n++;

    } while (bytes_read == TFTP_DATA_SIZE); // Stop when last block is less than 512 bytes

    fclose(file);
    printf("File %s sent Successfully!\n", filename);
}

// Function to handle RRQ (Read Request)
void rrq_h(int sockfd, struct sockaddr_in *server_addr, char *filename, const char *mode)
{
    socklen_t src_len = sizeof(*server_addr);
    char buffer[TFTP_BUF_SIZE];
    FILE *file;
    char filepath[PATH_LENGTH];
    ssize_t bytes_sent;
    int ch; // buffer-cleaner helper var

    printf("Enter the filename to download (netascii/octet): ");
    if (scanf("%255s", filename) != 1)
    {
        fprintf(stderr, "Error reading filename\n");
        return;
    }
    else if (!file_exists(filename))
    {
        printf("File doesn't exist, please try again\n");
        return;
    }

    // automatic determination of the file extension aka mode
    mode = get_mode(filename);

    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_CLIENT_DIR, filename);

    if (strcmp(mode, "netascii") == 0 || strcmp(mode, "octet") == 0)
    {
        file = fopen(filepath, "wb");
        {
            if (!file)
            {
                perror("Error opening file to write");
                return;
            }
        }
    }
    else
    {
        fprintf(stderr, "Invalid mode :%s\n", mode);
        return;
    }

    // Clear stdin buffer
    while ((ch = getchar()) != '\n' && ch != EOF)
        ;

    // Prepare RRQ packet
    memset(buffer, 0, sizeof(buffer)); // clearing the buffer
    uint16_t opcode = htons(TFTP_OPCODE_RRQ);
    memcpy(buffer, &opcode, sizeof(opcode));
    strcpy(buffer + 2, filename);
    strcpy(buffer + 2 + strlen(filename) + 1, mode);

    bytes_sent = sendto(sockfd, buffer, 2 + strlen(filename) + 1 + strlen(mode) + 1, 0,
                        (struct sockaddr *)server_addr, src_len);
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

    uint16_t expected_block = 1;
    uint16_t last_ack_block = 0;

    while (1)
    {
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

        uint16_t block_num = ((uint16_t)(uint8_t)buffer[2] << 8) | (uint16_t)(uint8_t)buffer[3];
        printf("Received DATA block %d, %zd bytes\n", block_num, recv_len - 4);

        if (block_num == expected_block)
        {
            printf("MATCHING BLOCK NUM\n");
            // Open file on first data block received

            // Write data payload
            fwrite(buffer + 4, 1, recv_len - 4, file);

            // Send ACK
            unsigned char ack_pkt[4] = {0, TFTP_OPCODE_ACK,
                                        (block_num >> 8) & 0xFF,
                                        block_num & 0xFF};
            ssize_t sent = sendto(sockfd, ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)server_addr, src_len);

            if (sent < 0)
            {
                perror("Failed to send ACK");
                break;
            }
            printf("Sent ACK for block %d\n", block_num);

            expected_block = block_num + 1;
            last_ack_block = block_num;
        }

        else if (block_num == ((expected_block - 1) & 0xFFFF)) // Duplicate of last block and resend ack if needed
        {
            printf("Duplicate block %u received — resending ACK\n", block_num);
            unsigned char ack_pkt[4] = {0, TFTP_OPCODE_ACK,
                                        (block_num >> 8) & 0xFF,
                                        block_num & 0xFF};
            ssize_t sent = sendto(sockfd, ack_pkt, 4, 0, (struct sockaddr *)server_addr, src_len);
            if (sent < 0)
            {
                perror("Failed to resend ACK");
                break;
            }
        }

        // check if last block of data
        if (recv_len < 4 + TFTP_DATA_SIZE)
        {
            printf("Sent all blocks %d\n", last_ack_block);
            printf("File %s has been downloaded successfully!\n", filename);
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
    char buffer[TFTP_BUF_SIZE];

    printf("Enter the filename you want to delete: ");
    scanf("%s", filename);

    socklen_t addr_len = sizeof(*server_addr);

    // Build DELETE request
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
