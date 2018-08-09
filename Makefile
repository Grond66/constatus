
BINDIR ?= $(CURDIR)
DEBUG ?=

SRCS = constatus.c
HDRS = constatus.h

CFLAGS = -Wall -pedantic $(shell pkg-config --cflags libconfig)
LDFLAGS = -lm -lpanel -lcurses -ldl $(shell pkg-config --libs libconfig)

.PHONY: all modules

all: modules $(BINDIR)/constatus

$(BINDIR)/constatus: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(DEBUG) -o $@ $(SRCS) $(LDFLAGS)

modules:
	$(MAKE) -C $(CURDIR)/modules
