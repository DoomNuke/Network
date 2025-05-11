#include <stdio.h>
#include <string.h>

#include "platform_exec.h"
#include "tftp_utils.h"


#ifdef _WIN32
#include <windows.h>


void handle_user_action(const char *filename, const char *mode) {
    printf("File received. Would you like to:\n");
    printf("1. Print file contents\n");
    printf("2. Execute the file\n");

    int choice;
    scanf("%d", &choice);

    if (choice == 1 && strcmp(mode, "netascii") == 0) {
        FILE *file = fopen(filename, "r");
        if (!file) {
            perror("Error opening file");
            return;
        }
        print_netascii_file(file);
        fclose(file);
    } else if (choice == 2) {
        char command[MAX_PATH];
        snprintf(command, sizeof(command), "tftp_root\\%s", filename);
        int ret = _spawnl(_P_NOWAIT, command, command, NULL); //creates a process, 2nd argument is the path, 3rd is the argv aka the program name, and null for no list of arguments
        if (ret == -1) {
            perror("Execution failed");
        }
    }
}

#else  // Unix

#include <unistd.h>

void handle_user_action(const char *filename, const char *mode) {
    printf("File received. Would you like to:\n");
    printf("1. Print file contents\n");
    printf("2. Execute the file\n");

    int choice;
    scanf("%d", &choice);

    if (choice == 1 && strcmp(mode, "netascii") == 0) {
        FILE *file = fopen(filename, "r");
        if (!file) {
            perror("Error opening file");
            return;
        }
        print_netascii_file(file);
        fclose(file);
    } else if (choice == 2) {
        char path[256];
        snprintf(path, sizeof(path), "./tftp_root/%s", filename);
        if (fork() == 0) {
            execl(path, filename, (char *)NULL);
            perror("Execution failed");
        }
    }
}

#endif