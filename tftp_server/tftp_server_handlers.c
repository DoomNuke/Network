#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>


#include "../utils/tftp_logger.h"
#include "../utils/tftp_utils.h"
#include "tftp_server_handlers.h"
#include "tftp_server.h"

#define TIMEOUT_MS 5000 //5 seconds timeout


//logger
void logger(const char *level, const char *format, ...) {
    // Open the log file in append mode
    FILE *file = fopen(LOG_FILE, "a");
    if (!file) {
        perror("Error opening log file");
        return;
    }

    time_t time_r;
    struct tm *timeinfo;
    char time_buf[50];

    time(&time_r);
    timeinfo = localtime(&time_r);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H-%M-%S", timeinfo); // Format date

    fprintf(file, "[%s [%s]] ", time_buf, level); // Write the timestamp and level

    // Handle the variable arguments
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);  // Write the formatted string
    va_end(args);

    fclose(file);  // Don't forget to close the file!
}
//sender ack
void send_ack(int sockfd, struct sockaddr_in *client_addr, tftp_packet_t *packet)
{
    int retries = 0;
    char ack_packet[4];

    //2 bytes for the ack packet 2 bytes for the block_n

    ack_packet[0] = 0; //opcode ack
    ack_packet[1] =  TFTP_OPCODE_ACK;
    ack_packet[2] = (packet->ack_pkt.block_n >> 8) & 0xFF; //for msb
    ack_packet[3] = packet->ack_pkt.block_n & 0xFF; //for lsb

    while(retries <= MAX_RETRIES) {
    if (sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)client_addr, sizeof(*client_addr)) < 0)
    {
        return; //exit the function, success
    }

    printf("Error: ACK not sent (Attempt:%d)\n", retries + 1);
    sleep(3); //wait for three seconds before retrying
    }

    printf("Error: Failed to send ACK after %d retries\n", retries);
}


int parse_wrq_packet(const char *buffer, char *filename, char *mode) {
    if (buffer[0] != 0 || buffer[1] != TFTP_OPCODE_WRQ) //checks if it the opcode is WRQ
        return 0;

    //extracts the file name//
    const char *p = buffer + 2;
    size_t fname_len = strlen(p);
    size_t mode_len = strlen(p + fname_len + 1); //checks for mode 

    if (fname_len == 0 || mode_len == 0) 
        return 0; //checks if the filename or mode are not null

    if (fname_len + mode_len + 4 > TFTP_BUF_SIZE)
        return 0; //checks for overflow
        
    //copy filename and mode//
    strncpy(filename, p, fname_len + 1);
    strncpy(mode, p + fname_len + 1, mode_len + 1);

    return (str_casecmp(mode, "netascii") == 0 || str_casecmp(mode, "octet") == 0);
}


/*
	writes data by the file mode 
 */
ssize_t write_file_data(FILE *file, const char *buffer, size_t size, const char *mode) {
    if (!file || !buffer || !mode) {
        return -1; // Invalid input
    }

    ssize_t bytes_written = 0;

    if (str_casecmp(mode, "netascii") == 0) {
        bytes_written = write_netascii(file, buffer, size);
    } else if (str_casecmp(mode, "octet") == 0) {
        bytes_written = write_octet(file, buffer, size);
    } else {
        return -1; // Unsupported mode
    }

    if (bytes_written != size) {
        return -1; // Writing error: not all bytes were written
    }

    return bytes_written; // Successfully written
}


//WRQ

void wrq_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode) {
    char filepath[PATH_LENGTH];
    FILE *file;
    tftp_packet_t packet;
    uint16_t block_n = 0;
    ssize_t recv_len;
    char buffer[TFTP_BUF_SIZE];
    int retries = 0;

    // Construct full file path
    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_ROOT_DIR, filename);

    // Check if file already exists
    if (access(filepath, F_OK) == 0) {
        logger("ERROR", "File already exists: %s", filename);
        // Send error packet (File already exists)
        char error_packet[5 + strlen("File already exists")];
        error_packet[0] = 0;
        error_packet[1] = TFTP_OPCODE_ERROR;
        error_packet[2] = 0;
        error_packet[3] = TFTP_OPCODE_EXISTS; // File already exists error code
        strcpy(error_packet + 4, "File already exists");
        sendto(sockfd, error_packet, sizeof(error_packet), 0, (struct sockaddr *)client_addr, client_len);
        return;
    }

    // Open file with appropriate mode
    if (str_casecmp(mode, "netascii") == 0) {
        file = fopen(filepath, "w");  // Text mode for netascii
    } else {
        file = fopen(filepath, "wb"); // Binary mode for octet
    }

    if (!file) {
        logger("ERROR", "Failed to create file: %s", filename);
        // Send error packet (Access violation)
        char error_packet[5 + strlen("Access violation")];
        error_packet[0] = 0;
        error_packet[1] = TFTP_OPCODE_ERROR;
        error_packet[2] = 0;
        error_packet[3] = TFTP_OPCODE_ACC_ERR; // Access violation error code
        strcpy(error_packet + 4, "Access violation");
        sendto(sockfd, error_packet, sizeof(error_packet), 0, (struct sockaddr *)client_addr, client_len);
        return;
    }

    logger("INFO", "Starting file transfer: %s in %s mode", filename, mode);

    // Send initial ACK for WRQ
    packet.ack_pkt.block_n = 0;
    send_ack(sockfd, client_addr, &packet);

    // Initialize pollfd structure
    struct pollfd fds[1];
    fds[0].fd = sockfd;
    fds[0].events = POLLIN;  // We want to poll for readable data

    // Receive and process data packets
    while (retries < MAX_RETRIES) {
        // Use poll() to handle the timeout and event checking
        int poll_ret = poll(fds, 1, TIMEOUT_MS);  //3 seconds timeout

        if (poll_ret < 0) {
            logger("ERROR", "Error with poll, retrying...");
            retries++;
            continue;
        }

        if (poll_ret == 0) {
            // Poll timeout
            retries++;
            logger("ERROR", "Timeout waiting for data packet, retrying... (%d/%d)", retries, MAX_RETRIES);
            continue;
        }

        // Now we know there's data available to receive
        if (fds[0].revents & POLLIN) {
            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)client_addr, &client_len);
            if (recv_len < 0) {
                retries++;
                logger("ERROR", "Error receiving data packet, retrying... (%d/%d)", retries, MAX_RETRIES);
                continue;
            }

            // Check if it's a data packet
            if (buffer[0] != 0 || buffer[1] != TFTP_OPCODE_DATA) {
                logger("ERROR", "Expected DATA packet, got different opcode. Retrying...");
                retries++;
                continue;
            }

            // Get block number
            uint16_t received_block = (buffer[2] << 8) | buffer[3];

            // Handle duplicate block (re-ACK the same block)
            if (received_block == block_n) {
                packet.ack_pkt.block_n = received_block;
                send_ack(sockfd, client_addr, &packet);
                continue; // Skip duplicated data
            }

            // Check if it's the expected block
            if (received_block != block_n + 1) {
                logger("ERROR", "Unexpected block number: %d (expected %d). Retrying...", received_block, block_n + 1);
                retries++;
                continue; // Check for the expected block
            }

            // Write data to file using mode
            size_t data_size = recv_len - 4; // Remove header size
            ssize_t bytes_written = write_file_data(file, buffer + 4, data_size, mode);

            if (bytes_written == -1) {
                logger("ERROR", "Error writing to file. Retrying...");
                retries++;
                continue;
            }

            // Send ACK
            packet.ack_pkt.block_n = received_block;
            send_ack(sockfd, client_addr, &packet);

            // Block number increment
            block_n = received_block;
            retries = 0;

            // Check if this was the last packet
            if (data_size < TFTP_DATA_SIZE) {
                break;
            }
        }
    }

    fclose(file);

    if (retries >= MAX_RETRIES) {
        logger("ERROR", "Transfer failed after %d retries: %s", MAX_RETRIES, filename);
        // Clean up the partial file
        remove(filepath);
    } else {
        logger("INFO", "File transfer completed successfully: %s", filename);
    }
}


//RRQ 
void rrq_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode) {
    char filepath[PATH_LENGTH];
    FILE *file;

    uint16_t block_n = 1;  // Start with block 1
    char buffer[TFTP_BUF_SIZE];
    int retries = 0;

    // Construct full file path
    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_ROOT_DIR, filename);

    // Check if file exists
    if (access(filepath, F_OK) == -1) {
        logger("ERROR", "File does not exist: %s", filename);
        // Send error packet (File not found)
        char error_packet[5 + strlen("File not found")];
        error_packet[0] = 0;
        error_packet[1] = TFTP_OPCODE_ERROR;
        error_packet[2] = 0;
        error_packet[3] = TFTP_OPCODE_NE; // File not found error code
        strcpy(error_packet + 4, "File not found");
        sendto(sockfd, error_packet, sizeof(error_packet), 0, (struct sockaddr *)client_addr, client_len);
        return;
    }

    // Open file with appropriate mode
    if (str_casecmp(mode, "netascii") == 0) {
        file = fopen(filepath, "r");  // Text mode for netascii
    } else {
        file = fopen(filepath, "rb"); // Binary mode for octet
    }

    if (!file) {
        logger("ERROR", "Failed to open file: %s", filename);
        // Send error packet (Access violation)
        char error_packet[5 + strlen("Access violation")];
        error_packet[0] = 0;
        error_packet[1] = TFTP_OPCODE_ERROR;
        error_packet[2] = 0;
        error_packet[3] = TFTP_OPCODE_ACC_ERR; // Access violation error code
        strcpy(error_packet + 4, "Access violation");
        sendto(sockfd, error_packet, sizeof(error_packet), 0, (struct sockaddr *)client_addr, client_len);
        return;
    }

    logger("INFO", "Processing read request: %s in %s mode", filename, mode);

    // Polling setup: We'll use poll() to wait for incoming ACKs with a timeout
    struct pollfd pfds[1];
    pfds[0].fd = sockfd;  // The socket descriptor
    pfds[0].events = POLLIN;  // We want to know when the socket is ready for reading

    // Read and send data
    while (retries < MAX_RETRIES) {
        size_t bytes_read;

        // Read data using appropriate mode
        if (str_casecmp(mode, "netascii") == 0) {
            bytes_read = read_netascii(file, buffer + 4, TFTP_DATA_SIZE);
        } else {
            bytes_read = read_octet(file, buffer + 4, TFTP_DATA_SIZE);
        }

        if (bytes_read == 0) {
            // End of file
            break;
        }

        // Prepare DATA packet
        buffer[0] = 0;
        buffer[1] = TFTP_OPCODE_DATA;
        buffer[2] = (block_n >> 8) & 0xFF;  // MSB
        buffer[3] = block_n & 0xFF;         // LSB

        // Send DATA packet
        if (sendto(sockfd, buffer, bytes_read + 4, 0, (struct sockaddr *)client_addr, client_len) < 0) {
            retries++;
            logger("ERROR", "Error sending DATA packet, retrying... (%d/%d)", retries, MAX_RETRIES);
            continue;
        }

        // Poll for incoming ACK with a timeout
        int poll_ret = poll(pfds, 1, TIMEOUT_MS); //timeout for 3 seconds
        
        if (poll_ret == 0) {
            // Timeout, no ACK received in time
            retries++;
            logger("ERROR", "Timeout occurred waiting for ACK, retrying... (%d/%d)", retries, MAX_RETRIES);
            continue;
        } else if (poll_ret < 0) {
            // Error in poll
            retries++;
            logger("ERROR", "Error in poll(), retrying... (%d/%d)", retries, MAX_RETRIES);
            continue;
        }

        // If we reach here, we have a valid event
        ssize_t ack_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)client_addr, &client_len);
        
        if (ack_len < 0) {
            retries++;
            logger("ERROR", "Error receiving ACK, retrying... (%d/%d)", retries, MAX_RETRIES);
            continue;
        }

        // Verify ACK
        if (buffer[0] != 0 || buffer[1] != TFTP_OPCODE_ACK) {
            retries++;
            logger("ERROR", "Expected ACK, got different opcode, retrying... (%d/%d)", retries, MAX_RETRIES);
            continue;
        }

        uint16_t ack_block = (buffer[2] << 8) | buffer[3];
        if (ack_block != block_n) {
            retries++;
            logger("ERROR", "Unexpected ACK block number: %d (expected %d), retrying... (%d/%d)", ack_block, block_n, retries, MAX_RETRIES);
            continue;
        }

        // Success, move to next block
        block_n++;
        retries = 0;

        // Check if this was the last packet
        if (bytes_read < TFTP_DATA_SIZE) {
            break;
        }
    }

    fclose(file);

    if (retries >= MAX_RETRIES) {
        logger("ERROR", "Read request failed after %d retries: %s", MAX_RETRIES, filename);
    } else {
        logger("INFO", "Read request completed: %s", filename);
    }
}



//DEL
void del_handler(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename, const char *mode) {
    char filepath[PATH_LENGTH];
    char buffer[TFTP_BUF_SIZE];
    int retries = 0;

    // Construct full file path
    snprintf(filepath, sizeof(filepath), "%s/%s", TFTP_ROOT_DIR, filename);

    // Check if file exists
    if (access(filepath, F_OK) == -1) {
        logger("ERROR", "File does not exist: %s", filename);
        // Send error packet (File not found)
        char error_packet[5 + strlen("File not found") + 1]; //the + 1 is for null terminator
        error_packet[0] = 0;
        error_packet[1] = TFTP_OPCODE_ERROR;
        error_packet[2] = 0;
        error_packet[3] = TFTP_OPCODE_NE; // File not found error code
        strcpy(error_packet + 4, "File not found");
        sendto(sockfd, error_packet, sizeof(error_packet), 0, (struct sockaddr *)client_addr, client_len);
        return;
    }

    // Check if we have permission to delete the file via W_OK as in (Write permissions)
    if (access(filepath, W_OK) == -1) { 
        logger("ERROR", "No permission to delete file: %s", filename);
        // Send error packet (Access violation)
        char error_packet[5 + strlen("Access violation") + 1]; //for null termination
        error_packet[0] = 0;
        error_packet[1] = TFTP_OPCODE_ERROR;
        error_packet[2] = 0;
        error_packet[3] = TFTP_OPCODE_ACC_ERR; // Access violation error code
        strcpy(error_packet + 4, "Access violation");
        sendto(sockfd, error_packet, sizeof(error_packet), 0, (struct sockaddr *)client_addr, client_len);
        return;
    }

    // Attempt to delete the file
    if (remove(filepath) != 0) {
        logger("ERROR", "Failed to delete file: %s", filename);
        // Send error packet (Disk full or allocation exceeded)
        char error_packet[5 + strlen("Disk full or allocation exceeded") + 1]; //for null termination
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
    buffer[0] = 0;
    buffer[1] = TFTP_OPCODE_ACK;
    buffer[2] = 0;  // Block number MSB
    buffer[3] = 0;  // Block number LSB

    // Initialize pollfd structure
    struct pollfd fds[1];
    fds[0].fd = sockfd;
    fds[0].events = POLLIN;  // We wait for any events happening to the file descriptor 

    // Send ACK and wait for confirmation
    while (retries < MAX_RETRIES) {
        // Use poll() to handle the timeout and event checking
        int poll_ret = poll(fds, 1, TIMEOUT_MS);  //Timeout

        if (poll_ret < 0) {
            logger("ERROR", "Error with poll, retrying...");
            retries++;
            continue;
        }

        if (poll_ret == 0) {
            // Poll timeout
            retries++;
            logger("ERROR", "Timeout waiting for ACK, retrying... (%d/%d)", retries, MAX_RETRIES);
            continue;
        }

        // Now we know there's data available to receive
        if (fds[0].revents & POLLIN) {
            // Send ACK
            if (sendto(sockfd, buffer, 4, 0, (struct sockaddr *)client_addr, client_len) < 0) {
                retries++;
                logger("ERROR", "Error sending ACK, retrying...");
                continue;
            }

            // Wait for confirmation
            ssize_t ack_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)client_addr, &client_len);
            if (ack_len < 0) {
                retries++;
                logger("ERROR", "Error receiving confirmation, retrying...");
                continue;
            }

            // Verify confirmation
            if (buffer[0] != 0 || buffer[1] != TFTP_OPCODE_ACK) {
                retries++;
                logger("ERROR", "Expected ACK, got different opcode, retrying...");
                continue;
            }

            // Success
            break;
        }
    }

    if (retries >= MAX_RETRIES) {
        logger("ERROR", "Delete operation failed after %d retries: %s", MAX_RETRIES, filename);
    }
}
