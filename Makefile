CC=gcc
CXX=g++
CPPFLAGS=-std=c++11

all: client server client.exe server.exe

client: client.c
	$(CC) -o $@ $@.c

client.exe: client.c
	$(CC) -o $@ client.c

server: server.cpp
	$(CXX) $(CPPFLAGS) -o $@ $@.cpp

server.exe: server.cpp
	$(CXX) $(CPPFLAGS) -o $@ server.cpp
