#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h> //for fork and excel
#include <sys/wait.h> //for wait

#include "platform_exec.h"
#include "tftp_utils.h"

/*
* 
*
* Windows compatibility will come in the next update 


#ifdef _WIN32
#include <windows.h>
#include <process.h>    // for _spawnl, _P_NOWAIT
#include <io.h>         // for _access()
#include <fcntl.h>      // for _O_RDONLY
#include <errno.h>      // for errno

void handle_user_action(const char *filename, const char *mode) {
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
*/

 // Unix

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/*
    a function that handles whatever the user chooses to do,
    right now the second option doesn't work well because i
    didn't implement an permission giver for the file,
    but option number 1 works
*/

void handle_user_action(const char *filename, const char *mode, const char *dir) {
    
    char full_path[PATH_LENGTH];

    //asking the user for the filename 
    if (filename == NULL) {
        char input[254];

        printf("Enter the filename: ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            fprintf(stderr, "Error reading filename\n");
            return;
        }

        // Remove newline
        input[strcspn(input, "\n")] = '\0';

        // no traversal
        if (strstr(input, "..") != NULL) {
        fprintf(stderr, "Invalid filename: directory traversal not allowed\n");
        return;
    }

        snprintf(full_path, sizeof(full_path), "%s/%s", dir, input);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, filename);
    }

    // Check if the file exists
    if (access(full_path, F_OK) != 0) {
        perror("File does not exist");
        return;
    }

    printf("File \"%s\" is available. Would you like to:\n", full_path);
    printf("1. Print file contents (netascii only)\n");
    printf("2. Execute the file\n");
    printf("Choice: ");

    int choice;
    if (scanf("%d", &choice) != 1) {
        fprintf(stderr, "Invalid input\n");
        while (getchar() != '\n' && !feof(stdin));  //clear buffer 
        return;
    }
    while (getchar() != '\n' && !feof(stdin));  //clear input

    if (choice == 1) {
        if (mode == NULL || strcmp(mode, "netascii") != 0) {
            fprintf(stderr, "This option requires netascii mode.\n");
            return;
        }

        FILE *file = fopen(full_path, "r");
        if (!file) {
            perror("Error opening file");
            return;
        }

        print_netascii_file(file);
        fclose(file);
    } else if (choice == 2) {
        if (access(full_path, X_OK) != 0) {
            perror("File don't got execute permissions\n");
            return;
        }

        pid_t pid = fork();
        if (pid == 0) {
            execl(full_path, filename ? filename : full_path, (char *)NULL); //if filename is null or full_path is null, throw error 
            perror("Execution failed");
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            wait(NULL);
        } else {
            perror("Fork failed");
        }
    } else {
        printf("Invalid choice.\n");
    }
}


