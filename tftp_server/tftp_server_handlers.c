#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/time.h>

#include "../utils/tftp_logger.h"
#include "../utils/tftp_utils.h"
#include "tftp_server_handlers.h"
#include "tftp_server.h"

#define TIMEOUT_MS 5000 // 5 seconds timeout

// logger
void logger(const char *level, const char *format, ...)
{
    // Open the log file in append mode
    FILE *file = fopen(LOG_FILE, "a");
    if (!file)
    {
        perror("Error opening log file");
        return;
    }

    time_t time_r;
    struct tm *timeinfo;
    char time_buf[50];

    time(&time_r);
    timeinfo = localtime(&time_r);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", timeinfo); // Format date

    fprintf(file, "[%s [%s]] ", time_buf, level); // Write the timestamp and level

    // Handle the variable arguments
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args); // Write the formatted string
    va_end(args);

    fclose(file); // Don't forget to close the file!
}

// sender ack
void send_ack(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, tftp_packet_t *packet)
{
    char ack_packet[4];

    ack_packet[0] = 0;
    ack_packet[1] = TFTP_OPCODE_ACK;
    ack_packet[2] = (packet->ack_pkt.block_n >> 8) & 0xFF;
    ack_packet[3] = packet->ack_pkt.block_n & 0xFF;

    if (sendto(sockfd, ack_packet, sizeof(ack_packet), 0,
               (struct sockaddr *)client_addr, client_len) < 0)
    {
        perror("Error sending ACK");
    }
    else
    {
        printf("Sent ACK for block %d\n", packet->ack_pkt.block_n);
    }
}

// function for file already exists
int f_exists(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename)
{
    char filepath[PATH_LENGTH];

    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_ROOT_DIR, filename);

    if (access(filepath, F_OK) == 0)
    {
        logger("ERROR", "File already exists: %s\n", filename);
        // Send error packet (File already exists)
        const char *error_msg = ("File already exists");
        int err_len = 4 + strlen(error_msg) + 1;
        char *error_packet = malloc(err_len);
        if (!error_packet)
        {
            logger("ERROR", "Memory allocation failed\n");
            return 0;
        }
        error_packet[0] = 0;
        error_packet[1] = TFTP_OPCODE_ERROR;
        error_packet[2] = 0;
        error_packet[3] = TFTP_OPCODE_EXISTS; // File already exists error code
        strcpy(error_packet + 4, error_msg);
        sendto(sockfd, error_packet, err_len, 0, (struct sockaddr *)client_addr, client_len);
        free(error_packet);
        return 0; // failure
    }
    return 1; // success
}

// function for file access
int f_acc(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename)
{
    char filepath[PATH_LENGTH];
    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_ROOT_DIR, filename);

    // Check file existence and readability
    if (access(filepath, R_OK) != 0)
    {
        logger("ERROR", "Access violation or file does not exist: %s\n", filename);
        // Send error packet (Access violation)
        char error_msg[] = "Access violation";
        size_t msg_len = strlen(error_msg);
        char error_packet[4 + msg_len + 1];

        error_packet[0] = 0;
        error_packet[1] = TFTP_OPCODE_ERROR;
        error_packet[2] = 0;
        error_packet[3] = TFTP_OPCODE_ACC_ERR; // Access violation error code
        strcpy(error_packet + 4, error_msg);

        sendto(sockfd, error_packet, sizeof(error_packet), 0, (struct sockaddr *)client_addr, client_len);
        return 0; // failure
    }
    return 1; // success
}

/*
    writes data by the file mode
 */
ssize_t write_file_data(FILE *file, const char *buffer, size_t size, const char *mode)
{
    if (!file || !buffer || !mode)
    {
        return -1; // Invalid input
    }

    ssize_t bytes_written = 0;

    if (str_casecmp(mode, "netascii") == 0)
    {
        bytes_written = write_netascii(file, buffer, size);
    }
    else if (str_casecmp(mode, "octet") == 0)
    {
        bytes_written = write_octet(file, buffer, size);
    }
    else
    {
        return -1; // Unsupported mode
    }

    if (bytes_written != size)
    {
        return -1; // Writing error: not all bytes were written
    }

    return bytes_written; // Successfully written
}

// WRQ
void wrq_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode)
{
    char filepath[PATH_LENGTH];
    FILE *file;
    tftp_packet_t packet;
    uint16_t block_n = 0;
    ssize_t recv_len;
    char buffer[TFTP_BUF_SIZE];
    int retries = 0;

    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_ROOT_DIR, filename);

    // Set socket receive timeout (5 seconds)
    struct timeval tv = {5, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt failed");
        // continue anyways, not fatal
    }

    // Check if the file exists, and if it does, exit (or send error to client, if desired)
    if (!f_exists(sockfd, client_addr, client_len, filename))
    {
        printf("The file already exists, %s\n", filename);
        logger("ERROR", "File %s exists already\n", filename);
        return;
    }

    // Open file for writing based on mode
    if (str_casecmp(mode, "netascii") == 0 || str_casecmp(mode, "octet") == 0)
    {
        file = fopen(filepath, "wb");
        if (!file)
        {
            logger("ERROR", "Failed to open file for writing\n");
            return;
        }
    }

    else
    {
        printf("Unsupported mode\n");
        logger("ERROR", "Unsupported mode for file %s\n", filename);
        return;
    }

    /*
        lets the client know it's
        ready to receive data
    */

    packet.ack_pkt.block_n = 0;
    send_ack(sockfd, client_addr, client_len, &packet);

    // Now start receiving data packets
    while (retries < MAX_RETRIES)
    {
        // Wait for WRQ packet
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)client_addr, &client_len);

        printf("Received %zd bytes.\n", (ssize_t)recv_len);

        printf("Raw buffer: %02x %02x %02x %02x ...\n", buffer[0], buffer[1], buffer[2], buffer[3]);

        if (recv_len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Timeout — retry sending the same data
                retries++;
                fprintf(stderr, "Timeout waiting for ACK for block %d. Retrying (%d/%d)...\n",
                        block_n, retries, MAX_RETRIES);

                packet.ack_pkt.block_n = block_n; 
                send_ack(sockfd, client_addr, client_len, &packet);
                
                continue;
            }
            else
            {
                perror("recvfrom failed");
                return;
            }
            break;
        }

        // Validate the opcode is DATA (TFTP_OPCODE_DATA)
        if (buffer[1] != TFTP_OPCODE_DATA)
        {
            printf("Unexpected opcode: %d\n", buffer[1]);
            retries++; // Increment retries for unexpected packets
            continue;  // Skip this packet
        }

        uint16_t recv_block_n = ((uint16_t)(uint8_t)buffer[2] << 8) | (uint16_t)(uint8_t)buffer[3];

        if (recv_block_n == block_n + 1) //valid data block
        {
            ssize_t data_len = recv_len - 4; // exclude header (4 bytes)
            ssize_t written = write_file_data(file, buffer + 4, data_len, mode);

            if (written < data_len)
            {
                logger("ERROR", "Failed to write full block to file\n");
                break;
            }

            block_n = recv_block_n;           // Update expected block number
            packet.ack_pkt.block_n = block_n; // start incrementing block_n to send ack for each data block
            send_ack(sockfd, client_addr, client_len, &packet);
            
            retries = 0; // Reset retries after successful write

            if (data_len < TFTP_DATA_SIZE) //EOF
            {
                printf("Last block received. Transfer complete.\n");
                break;
            }
        }
        else if (recv_block_n == block_n) //
        {
            // duplicate data block retransmit ack for expected data block
            printf("Received out-of-order block %d, retransmitting ACK for block %d\n", recv_block_n, block_n);
            packet.ack_pkt.block_n = block_n;
            send_ack(sockfd, client_addr, client_len, &packet);
        }
    }

    fclose(file); // Close the file once done
    printf("File transfer completed: %s\n", filename);
    logger("INFO", "File has been created: %s\n", filename);
}

// RRQ
void rrq_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode)
{
    char filepath[PATH_LENGTH];
    FILE *file;
    uint16_t block_n = 1;
    char buffer[TFTP_BUF_SIZE];
    unsigned char ack_buf[4]; // Separate buffer for ACKs
    ssize_t bytes_read = 0;

    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_ROOT_DIR, filename);

    /* checking for permissions and access validation */
    if (!f_acc(sockfd, client_addr, client_len, filename))
    {
        return;
    }

    if (str_casecmp(mode, "netascii") == 0)
        file = fopen(filepath, "r");
    else if (str_casecmp(mode, "octet") == 0)
        file = fopen(filepath, "rb");
    else
        return;

    if (!file)
        return;

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    logger("INFO", "File opened successfully: %s (%ld bytes)\n", filename, file_size);

    // Set socket receive timeout (5 seconds)
    struct timeval tv = {5, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt failed");
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
            ssize_t sent_len = sendto(sockfd, buffer, bytes_read + 4, 0,
                                      (struct sockaddr *)client_addr, client_len);
            if (sent_len < 0)
            {
                perror("sendto failed");
                fclose(file);
                return;
            }

            // Wait for ACK
            ssize_t recv_len = recvfrom(sockfd, ack_buf, sizeof(ack_buf), 0,
                                        (struct sockaddr *)client_addr, &client_len);

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

            // If we get here, either bad ACK or wrong block number
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
    logger("INFO", "File sent successfully: %s (%ld bytes)\n", filename, file_size);
}

// DEL
void del_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename)
{
    char filepath[PATH_LENGTH];
    tftp_packet_t packet;

    // Construct full file path
    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_ROOT_DIR, filename);

    // file access permissions check
    f_acc(sockfd, client_addr, client_len, filename);

    // Attempt to delete the file
    if (remove(filepath) != 0)
    {
        logger("ERROR", "Failed to delete file: %s\n", filename);
        // Send error packet (Disk full or allocation exceeded)
        char error_packet[5 + strlen("Disk full or allocation exceeded") + 1]; // for null termination
        error_packet[0] = 0;
        error_packet[1] = TFTP_OPCODE_ERROR;
        error_packet[2] = 0;
        error_packet[3] = TFTP_OPCODE_F; // Disk full or allocation exceeded error code
        strcpy(error_packet + 4, "Disk full or allocation exceeded");

        sendto(sockfd, error_packet, sizeof(error_packet), 0, (struct sockaddr *)client_addr, client_len);
        return;
    }

    logger("INFO", "File deleted successfully: %s\n", filename);

    // Send success response (ACK with block number 0)
    packet.ack_pkt.block_n = 0;
    send_ack(sockfd, client_addr, client_len, &packet);

    printf("File %s has been deleted successfully", filename);
}
