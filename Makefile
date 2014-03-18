# Makefile for OS Project2: pft.cpp
TAR = ex2.tar
TAR_CMD = tar cvf
CC = g++ -Wall -std=c++11

all: lib

lib: pft.o
	ar rcs libpft.a pft.o

pft: pft.o
	$(CC) pft.o -o pft

pft.o: pft.cpp pft.h
	$(CC) -c pft.cpp -o pft.o
	
clean:
	rm -f $(TAR) pft.o libpft.a pft 

tar: pft.cpp Makefile README
	$(TAR_CMD) $(TAR) pft.cpp Makefile README