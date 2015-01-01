CC=gcc

all: client server

client: client.c
	$(CC) -o $@ $@.c

server: server.c
	$(CC) -o $@ $@.c
