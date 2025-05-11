# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99
LDFLAGS = 

# Directories
SRC_DIR = .
OBJ_DIR = obj
BIN_DIR = bin

# Source files and object files
SRCS = \
    $(SRC_DIR)/common/tftp_common.c \
    $(SRC_DIR)/tftp_client/tftp_client_handlers.c \
    $(SRC_DIR)/tftp_client/tftp_client.c \
    $(SRC_DIR)/tftp_server/tftp_handlers.c \
    $(SRC_DIR)/tftp_server/tftp_server.c \
    $(SRC_DIR)/utils/platform_exec.c \
    $(SRC_DIR)/utils/tftp_utils.c

OBJS = $(SRCS:.c=.o)

# Executable name
TARGET = $(BIN_DIR)/tftp

# Make all target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)

# Compile C files to object files
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Run the executable
run: $(TARGET)
	./$(TARGET)

# Phony targets (these don't represent actual files)
.PHONY: all clean run