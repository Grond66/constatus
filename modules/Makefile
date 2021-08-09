
MODDIR = $(CURDIR)

DEBUG ?=
LIBDIR ?= $(CURDIR)
INCDIR ?= $(CURDIR)

CFLAGS = -Wall -pedantic -I $(INCDIR)
LDFLAGS = -lcurses -lm -lbatt -L $(LIBDIR)
ARCH = $(shell uname -m)

ifeq ($(ARCH), x86_64)
   override CFLAGS += -fPIC
endif

MODS = onoff clock linux_battery
MOD_OBJS = $(addprefix $(CURDIR)/,$(addsuffix .so,$(MODS)))

.PHONY: modules

modules: $(MOD_OBJS)

$(MODDIR)/%.so: $(MODDIR)/%.c $(MODDIR)/../constatus.h
	$(CC) $(CFLAGS) $(DEBUG) -I $(CURDIR)/.. -shared -o $@ $^ $(LDFLAGS)
