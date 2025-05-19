CC = gcc
CFLAGS = -Wall -g -fno-common

# Directories
COMMON_DIR = common
UTILS_DIR = utils
CLIENT_DIR = tftp_client
SERVER_DIR = tftp_server

# File lists (excluding tftp_common.c since it's just a header)
UTILS_FILES = $(UTILS_DIR)/tftp_utils.c $(UTILS_DIR)/platform_exec.c
CLIENT_FILES = $(CLIENT_DIR)/tftp_client.c $(CLIENT_DIR)/tftp_client_handlers.c
SERVER_FILES = $(SERVER_DIR)/tftp_server.c $(SERVER_DIR)/tftp_server_handlers.c

# Object files
UTILS_OBJS = $(UTILS_FILES:.c=.o)
CLIENT_OBJS = $(CLIENT_FILES:.c=.o)
SERVER_OBJS = $(SERVER_FILES:.c=.o)

# Output executables
CLIENT_EXEC = tftp_client_r
SERVER_EXEC = tftp_server_r

# Targets
all: $(CLIENT_EXEC) $(SERVER_EXEC)

# Compile tftp_client
$(CLIENT_EXEC): $(CLIENT_OBJS) $(UTILS_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile tftp_server
$(SERVER_EXEC): $(SERVER_OBJS) $(UTILS_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# General rule to compile .c to .o with path handling
$(UTILS_DIR)/%.o: $(UTILS_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(CLIENT_DIR)/%.o: $(CLIENT_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_DIR)/%.o: $(SERVER_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up object files and executables
clean:
	rm -f $(UTILS_DIR)/*.o $(CLIENT_DIR)/*.o $(SERVER_DIR)/*.o $(CLIENT_EXEC) $(SERVER_EXEC)

.PHONY: all clean
