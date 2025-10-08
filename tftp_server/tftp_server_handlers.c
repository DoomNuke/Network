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
        logger("ERROR", "File already exists: %s", filename);
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
    uint16_t block_n = 1;
    ssize_t recv_len;
    char buffer[TFTP_BUF_SIZE];
    int retries = 0;

    snprintf(filepath, PATH_LENGTH, "%s/%s", TFTP_ROOT_DIR, filename);

    // checks if the file exists, and if it does, it exits
    if (!f_exists(sockfd, client_addr, client_len, filename))
        return;

    if (str_casecmp(mode, "netascii") == 0)
        file = fopen(filepath, "w");
    else if (str_casecmp(mode, "octet") == 0)
        file = fopen(filepath, "wb");
    else
        return;

    if (!file)
    {
        logger("ERROR", "Failed to open file for writing");
        return;
    }


    // Set socket receive timeout (5 seconds)
    struct timeval tv = {5, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt failed");
        // continue anyways, not fatal
    }
    while (retries < MAX_RETRIES)
    {
        socklen_t addr_len = sizeof(*client_addr);
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)client_addr, &addr_len);
        if (recv_len < 0)
        {
            perror("recvfrom failed");
            retries++;
            continue;
        }

        printf("Received %zd bytes. Opcode bytes: 0x%02x 0x%02x\n",
        (ssize_t)recv_len ,(unsigned char)buffer[0],(unsigned char)buffer[1]);

        if (recv_len < 4)
        {
            printf("Packet too short\n");
            retries++;
            continue;
        }

        if (buffer[1] != TFTP_OPCODE_DATA)
        {
            printf("Unexpected opcode: %d\n", buffer[1]);
            retries++;
            continue;
        }

        uint16_t recv_block_n = (buffer[2] << 8) | buffer[3];

        if (recv_block_n == block_n + 1)
        {
            ssize_t data_len = recv_len - 4;
            ssize_t written = write_file_data(file, buffer + 4, data_len, mode);
            if (written < data_len)
            {
                logger("ERROR", "Failed to write full block to file");
                break;
            }

            block_n = recv_block_n;
            retries = 0;

            // ACK back to client
            packet.ack_pkt.block_n = block_n;
            send_ack(sockfd, client_addr, client_len, &packet);

            if (data_len < TFTP_DATA_SIZE)
                break; // End of file
        }
        else if (recv_block_n == block_n)
        {
            // Duplicate DATA block (retransmit ACK)
            packet.ack_pkt.block_n = block_n;
            send_ack(sockfd, client_addr, client_len, &packet);
        }
    }

    fclose(file);
    printf("File transfer completed: %s\n", filename);
}

// RRQ
void rrq_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode)
{
    char filepath[PATH_LENGTH];
    FILE *file;
    uint16_t block_n = 1;
    char buffer[TFTP_BUF_SIZE];
    int retries = 0;
    ssize_t bytes_read = 0;
    ssize_t last_bytes_read = 0;

    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_ROOT_DIR, filename);

    // checkup to see if permissions are set
    if (!f_acc(sockfd, client_addr, client_len, filename))
        return;

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

    logger("INFO", "File opened successfully: %s (%ld bytes)", filename, file_size);

    // Set socket receive timeout (5 seconds)
    struct timeval tv = {5, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt failed");
        // continue anyways, not fatal
    }

    while (1)
    {
        if (str_casecmp(mode, "netascii") == 0)
            bytes_read = read_netascii(file, buffer + 4, TFTP_DATA_SIZE);
        else if (str_casecmp(mode, "octet") == 0)
            bytes_read = read_octet(file, buffer + 4, TFTP_DATA_SIZE);

        last_bytes_read = bytes_read;

        buffer[0] = 0;
        buffer[1] = TFTP_OPCODE_DATA;
        buffer[2] = (block_n >> 8) & 0xFF;
        buffer[3] = block_n & 0xFF;

        ssize_t sent_len = sendto(sockfd, buffer, bytes_read + 4, 0,
                                  (struct sockaddr *)client_addr, client_len);
        if (sent_len < 0)
            break;

        while (1)
        {
            socklen_t addr_len = sizeof(*client_addr);
            ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                        (struct sockaddr *)client_addr, &addr_len);
            if (recv_len < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // Timeout occurred — resend last data packet
                    if (++retries >= MAX_RETRIES)
                    {
                        fprintf(stderr, "Max retries reached. Aborting transfer.\n");
                        fclose(file);
                        return;
                    }
                    ssize_t resend_len = sendto(sockfd, buffer, bytes_read + 4, 0,
                                                (struct sockaddr *)client_addr, addr_len);
                    if (resend_len < 0)
                    {
                        perror("Failed to resend data packet");
                        fclose(file);
                        return;
                    }
                    continue; // wait again for ACK
                }
                else
                {
                    perror("recvfrom error");
                    fclose(file);
                    return;
                }
            }

            // Got something — check if it's a valid ACK
            if (recv_len < 4 || buffer[1] != TFTP_OPCODE_ACK)
                continue;

            uint16_t ack_block = (buffer[2] << 8) | buffer[3];
            if (ack_block != block_n)
            {
                continue;
            }

            // ACK received correctly
            retries = 0;
            break; // exit inner while and send next block

            // If last block was exactly TFTP_DATA_SIZE, send a zero-byte data packet
            if (last_bytes_read == TFTP_DATA_SIZE)
            {
                buffer[0] = 0;
                buffer[1] = TFTP_OPCODE_DATA;
                buffer[2] = (block_n >> 8) & 0xFF;
                buffer[3] = block_n & 0xFF;

                ssize_t sent_len = sendto(sockfd, buffer, 4, 0, (struct sockaddr *)client_addr, client_len);
                if (sent_len < 0)
                {
                    perror("Failed to send last zero-byte packet");
                    fclose(file);
                    return;
                }

                socklen_t addr_len = client_len;
                ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                            (struct sockaddr *)client_addr, &addr_len);
                if (recv_len < 0)
                {
                    perror("recvfrom failed");
                    return;
                }
                else if (recv_len < 4)
                {
                    fprintf(stderr, "Received packet too small: %zd bytes\n", recv_len);
                }
                else
                {
                    // At least 4 bytes received, can safely access opcode and block number
                    uint16_t recv_opcode = (buffer[0] << 8) | buffer[1];
                    if (recv_opcode != TFTP_OPCODE_ACK)
                    {
                        fprintf(stderr, "Unexpected opcode %d received\n", recv_opcode);
                        return;
                    }
                    else
                    {
                        uint16_t ack_block = (buffer[2] << 8) | buffer[3];
                        printf("Received ACK for block %d\n", ack_block);
                        // process ACK block number as needed
                    }
                }
            }
            block_n++;
            break;
        }
    }

    if (bytes_read == TFTP_DATA_SIZE)
    {
        buffer[0] = 0;
        buffer[1] = TFTP_OPCODE_DATA;
        buffer[2] = (block_n >> 8) & 0xFF;
        buffer[3] = block_n & 0xFF;

        ssize_t sent_len = sendto(sockfd, buffer, 4, 0,
                                  (struct sockaddr *)client_addr, client_len);
        if (sent_len < 0)
        {
            perror("Failed to send last zero-byte data packet");
            fclose(file);
            return;
        }
        printf("Sent last zero-byte DATA packet for block %d\n", block_n);

        // Now wait for the final ACK for this block
        socklen_t addr_len = client_len;
        ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                    (struct sockaddr *)client_addr, &addr_len);
        if (recv_len < 0)
        {
            perror("recvfrom failed on last ACK");
            // handle retry logic here if needed
        }
        else if (recv_len < 4)
        {
            fprintf(stderr, "Received too small packet on last ACK: %zd bytes\n", recv_len);
        }
        else
        {
            uint16_t recv_opcode = (buffer[0] << 8) | buffer[1];
            uint16_t ack_block = (buffer[2] << 8) | buffer[3];
            if (recv_opcode == TFTP_OPCODE_ACK && ack_block == block_n)
            {
                printf("Received final ACK for block %d\n", ack_block);
            }
            else
            {
                fprintf(stderr, "Unexpected packet on last ACK: opcode %d block %d\n", recv_opcode, ack_block);
            }
        }
        fclose(file);
    }
}

// DEL
void del_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode)
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
        logger("ERROR", "Failed to delete file: %s", filename);
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

    logger("INFO", "File deleted successfully: %s", filename);

    // Send success response (ACK with block number 0)
    packet.ack_pkt.block_n = 0;
    send_ack(sockfd, client_addr, client_len, &packet);

    printf("File %s has been deleted successfully", filename);
}
