TARGET1 = udp_server_bd
include ../common.mk

CC = gcc
CFLAGS = -Wall -I/usr/include/mysql
LDFLAGS = -L/usr/lib/x86_64-linux-gnu -lpthread -lmysqlclient

all: udp_server_bd

udp_server_bd: udp_server_bd.o
	$(CC) -o udp_server_bd udp_server_bd.o $(LDFLAGS)

udp_server_bd.o: udp_server_bd.c
	$(CC) $(CFLAGS) -c udp_server_bd.c

clean:
	rm -f *.o udp_server_bd