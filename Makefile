CC=gcc
CXX=g++

all: client server

client: client.c
	$(CC) -o $@ $@.c

server: server.cpp
	$(CXX) -o $@ $@.cpp
