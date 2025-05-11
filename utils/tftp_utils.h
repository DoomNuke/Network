#ifndef TFTP_UTILS_H
#define TFTP_UTILS_H


#include <signal.h>

//not using normal tftp port because I always gotta sudo :)
#define TFTP_PORT         6969 
#define TFTP_BUF_SIZE  516  // 512 bytes data + 4 bytes header
#define TFTP_DATA_SIZE    512

//definitions of each mode code
#define TFTP_OPCODE_RRQ   1 //RRQ
#define TFTP_OPCODE_WRQ   2 //WRQ
#define TFTP_OPCODE_DATA  3 //data
#define TFTP_OPCODE_ACK   4 //ack
#define TFTP_OPCODE_ERROR 5 //general error
#define TFTP_OPCODE_EXISTS 6 // exists
#define TFTP_OPCODE_ACC_ERR 7 // access error
#define TFTP_OPCODE_DEL 8 // delete
#define TFTP_OPCODE_NE 9 //doesn't exist 
#define TFTP_OPCODE_F 10 //disk full


//sigints for both server and clients
volatile sig_atomic_t server_running = 1;
void sigint_server(int sig);
volatile sig_atomic_t client_running = 1;
void sigint_client(int sig);

//root folder, every single file goes there and every operation happens in that folder
#define PATH_LENGTH 256
int dir_exist();

//NetAscii mode handler
size_t read_netascii(FILE *file, char *buffer, size_t max_size);
size_t write_netascii(FILE *file, const char *buffer, size_t size);

//octet mode handler
size_t read_octet(FILE *file, char *buffer, size_t max_size);
size_t write_octet(FILE *file, const char *buffer, size_t size);

//prints netascii contents
void print_netascii_file(FILE *file); 

//checks if the folder exists or not
int dir_exist();
//checks if the file exists 
int file_exists(const char *filename);

//additional tools
int str_casecmp(const char *s1, const char *s2);

#endif

