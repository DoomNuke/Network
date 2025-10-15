#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>

#include "tftp_utils.h"
#include "../tftp_server/tftp_server.h"

/*
This part of the code is for path check, the mode selection between netascii
and octet mode, it works on unix based OS's and windows too
*/

/* additional feature to be made in the future----

#ifdef _WIN32
	#include <direct.h> //for mkdir in windows
	#define MKDIR(TFTP_ROOT_DIR) _mkdir (PATH)
*/
	#include <unistd.h> //for unix
	#define MKDIR(TFTP_ROOT_DIR) mkdir(PATH, 0777) //mkdir with full permissions


int file_exists(const char *filename){
	return access(filename, F_OK) != 1; //checks if the file exists
}




int dir_exist(const char *dir_path){
    struct stat st = {0}; // setting up stat

    // check if the folder exists
    if (stat(dir_path, &st) == -1) {
        if (errno == ENOENT) {
            // directory doesn't exist, create it
            if (mkdir(dir_path, 0777) == 0) {
                printf("Directory created successfully: %s\n", dir_path);
                return 1; //success
            } else {
                perror("Error creating the directory");
                return 0; //failed
            }
        } else {
            //some other error
            perror("Error checking directory");
            return 0; // Failure
        }
    } else {
        //directory exists
        printf("Directory exists: %s\n", dir_path);
        return 1;
    }
}



/*
    clrf - used for windows too
    with preventing overflow (adding 1 byte safeguarding max_size)
*/
size_t read_netascii(FILE *file, char *buf, size_t max_size)
{
    size_t bytes_read = 0;
    int ch;

    while ((ch = fgetc(file)) != EOF)
    {
        if (ch == '\n')
        {
            if (bytes_read + 2 > max_size) break;
            buf[bytes_read++] = '\r';
            buf[bytes_read++] = '\n'; 
        }
        else
        {
            if (bytes_read + 1 > max_size) break;
            buf[bytes_read++] = (char)ch;
        }
    }

    return bytes_read;
}


//prints out the content
void print_netascii_file(FILE *file) {
    char buffer[TFTP_BUF_SIZE];
    size_t bytes_read;

    // Rewind in case the file pointer is not at the beginning
    rewind(file);

    while ((bytes_read = read_netascii(file, buffer, sizeof(buffer))) > 0) {
        buffer[bytes_read] = '\0';  // Ensure null-termination
        printf("%s\n", buffer);
    }
}

//writes netascii
size_t write_netascii(FILE *file, const char *buf, size_t size)
{
	size_t bytes_written = 0;

	for (size_t i = 0; i < size; i++)
    {
        //skip CR characters (0x0D)
        if (buf[i] == '\r')
        {
            bytes_written++;
            continue;
        }

        //write the characters including LF

        if(putc(buf[i], file) == EOF)
        {
            return bytes_written;
        }
        
        bytes_written++;
    }
    return bytes_written;
}


// checks if it's octet

size_t read_octet(FILE *file, char *buf, size_t max_size)
{
	return fread(buf, 1, max_size, file); // Read raw binary data
}

size_t write_octet(FILE *file, const char *buf, size_t size)
{
	return fwrite(buf, 1, size, file); // Write raw binary data
}

const char *get_mode(const char *filename)
{
    const char *ext = strchr(filename, '.');
    if (!ext || ext == filename)
    {
        return "octet";
    }

    ext++; //skip the dot

    //list of extensions for netascii
    if (strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "c") == 0 || strcasecmp(ext, "h") == 0 ||
        strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0 || strcasecmp(ext, "py") == 0 ||
        strcasecmp(ext, "csv") == 0 || strcasecmp(ext, "json") == 0 || strcasecmp(ext, "xml") == 0)
    {
        return "netascii";
    }

    return "octet"; //by default it'll return octet
}

//for usage of strcasecmp - for both unix and windows 
int str_casecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2))
            return (unsigned char)*s1 - (unsigned char)*s2;
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

