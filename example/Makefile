# Compiler
CC = g++

# Compiler Flags
CFLAGS = -std=c++11

# Target Executables
SERVER_TARGET = server
CLIENT_TARGET = client

# Source Files
SERVER_SOURCE = server.cpp
CLIENT_SOURCE = client.cpp

# Create the Target Executables
all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_SOURCE)
	$(CC) $(CFLAGS) -o $(SERVER_TARGET) $(SERVER_SOURCE)

$(CLIENT_TARGET): $(CLIENT_SOURCE)
	$(CC) $(CFLAGS) -o $(CLIENT_TARGET) $(CLIENT_SOURCE)

# Clean Targets
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)

