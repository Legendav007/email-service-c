# MiniMail - multi-client email service over TCP sockets
#
#   make            build server and client
#   make test       build and run the automated protocol test suite
#   make clean      remove build artefacts and test data

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
LDFLAGS ?=
LDLIBS  ?= -lpthread

SERVER_SRC = server.c storage.c protocol.c
CLIENT_SRC = client.c protocol.c
COMMON_HDR = common.h storage.h

all: server client

server: $(SERVER_SRC) $(COMMON_HDR)
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS) $(LDLIBS)

client: $(CLIENT_SRC) $(COMMON_HDR)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC) $(LDFLAGS)

test: all
	./tests/run_tests.sh

clean:
	rm -f server client
	rm -rf tests/tmp

.PHONY: all test clean
