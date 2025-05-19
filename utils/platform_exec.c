#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h> //for fork and excel
#include <sys/wait.h> //for wait

#include "platform_exec.h"
#include "tftp_utils.h"

#define MAX_PATH_LENGTH 260

#ifdef _WIN32
#include <windows.h>
#include <process.h>    // for _spawnl, _P_NOWAIT
#include <io.h>         // for _access()
#include <fcntl.h>      // for _O_RDONLY
#include <errno.h>      // for errno

void handle_user_action(const char *filename, const char *mode) {
    printf("File received. Would you like to:\n");
    printf("1. Print file contents (only if mode is netascii)\n");
    printf("2. Execute the file\n");

    int choice;
    if (scanf("%d", &choice) != 1) {
        fprintf(stderr, "Invalid input\n");
        return;
    }

    // Build the full path using Windows directory separator
    char full_path[MAX_PATH_LENGTH];
    snprintf(full_path, sizeof(full_path), "tftp_root\\%s", filename);

    // If the file doesn't exist, return
    if (_access(full_path, 0) != 0) {
        perror("File does not exist");
        return;
    }

    if (choice == 1 && strcmp(mode, "netascii") == 0) {
        // Open the file and print it in netascii format
        FILE *file = fopen(full_path, "r");
        if (!file) {
            perror("Error opening file");
            return;
        }
        print_netascii_file(file);
        fclose(file);
    } else if (choice == 2) {
        // Execute the file using _spawnl
        int ret = _spawnl(_P_NOWAIT, full_path, filename, NULL);
        if (ret == -1) {
            perror("Execution failed");
        }
    } else {
        printf("Invalid choice or unsupported mode.\n");
    }
}

#else  // Unix

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

void handle_user_action(const char *filename, const char *mode) {
    char full_path[MAX_PATH_LENGTH];

    // If filename is NULL, ask for the filename from the user
    if (filename == NULL) {
        printf("Enter the filename: ");
        if (scanf("%255s", full_path) != 1) {
            fprintf(stderr, "Error reading filename\n");
            return;
        }
    } else {
        // Build the full path using Unix directory separator
        snprintf(full_path, sizeof(full_path), "tftp_root/%s", filename);
    }

    // Check if the file exists
    if (access(full_path, F_OK) != 0) {
        perror("File does not exist");
        return;
    }

    printf("File \"%s\" is available. Would you like to:\n", filename);
    printf("1. Print file contents (netascii only)\n");
    printf("2. Execute the file\n");
    printf("Choice: ");

    int choice;
    if (scanf("%d", &choice) != 1) {
        fprintf(stderr, "Invalid input\n");
        return;
    }

    // Option 1: Print file contents in netascii format
    if (choice == 1 && strcmp(mode, "netascii") == 0) {
        FILE *file = fopen(full_path, "r");
        if (!file) {
            perror("Error opening file");
            return;
        }
        print_netascii_file(file);
        fclose(file);
    } 
    // Option 2: Execute the file
    else if (choice == 2) {
        // Check if the file is executable
        if (access(full_path, X_OK) != 0) {
            perror("File is not executable");
            return;
        }
        
        // Fork and execute the file
        if (fork() == 0) {
            execl(full_path, filename, (char *)NULL);  // Execute the file
            perror("Execution failed");
            exit(EXIT_FAILURE);
        } else {
            wait(NULL);  // Parent waits for child process to finish
        }
    } else {
        printf("Invalid choice or unsupported mode.\n");
    }
}

#endif

