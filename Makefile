# Define compiler
CC=gcc

# Define compiler flags
CFLAGS=-Wall

# Target executable
TARGET = a.out

# Source file
SRC = BRABANTE_JA_code.c

# Default target to build the executable
all: $(TARGET)

# Compile the executable
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -lpthread -lm

# Run batch/script execution
execute: all
	./execute_runs.sh

# Clean up the generated files
clean:
	rm -f $(TARGET) 

.PHONY: all run clean