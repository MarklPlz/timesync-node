# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -pedantic -O3 -std=c23 -D_POSIX_C_SOURCE=199309L

# Source files
SRC = src/timesync-node.c

# Object files
OBJ = $(SRC:.c=.o)

# Executables
TARGET = timesync-node

# Default target
all: $(TARGET)

# Compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link object files into executables
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET)

# Clean objects and executables
clean:
	rm -f $(OBJ) $(TARGET)

