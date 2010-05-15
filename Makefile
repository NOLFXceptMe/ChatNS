CC=gcc
CFLAGS=-Wall -g -DNDEBUG
LD=gcc
LDFLAGS=-lpthread
BIN=ChatServer ChatClient

all: $(BIN)

clean:
	rm -f ChatServer ChatClient
