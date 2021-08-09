
BINDIR ?= $(CURDIR)
DEBUG ?=

SRCS = constatus.c module_api.c
HDRS = constatus.h
BIN = $(BINDIR)/constatus

CFLAGS = -Wall -pedantic $(shell pkg-config --cflags libconfig)
LDFLAGS = -rdynamic -lm -lpanel -lcurses -ldl $(shell pkg-config --libs libconfig)

.PHONY: all clean modules

all: modules $(BIN)

$(BIN): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(DEBUG) -o $@ $(SRCS) $(LDFLAGS)

modules:
	$(MAKE) -C $(CURDIR)/modules

clean:
	rm -f $(BIN)
	$(MAKE) -C $(CURDIR)/modules clean
