CC=gcc
CXX=g++
CPPFLAGS=-std=c++11

all: client server

client: client.c
	$(CC) -o $@ $@.c

server: server.cpp
	$(CXX) $(CPPFLAGS) -o $@ $@.cpp
