prog = 5pid_tcp_udp_server
LFLAGS = -lrt -lpthread
CC = gcc

all: $(prog).o
		$(CC) -Wall -o $(prog) $(prog).c $(LFLAGS)

cl: clean

clean:
		rm -f $(prog) $(prog).o