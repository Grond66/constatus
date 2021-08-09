
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
MOD_OBJS = $(addprefix $(MODDIR)/,$(addsuffix .so,$(MODS)))

.PHONY: modules clean

modules: $(MOD_OBJS)

clean:
	rm -f $(MOD_OBJS)

$(MODDIR)/%.so: $(CURDIR)/%.c $(CURDIR)/../constatus.h
	$(CC) $(CFLAGS) $(DEBUG) -I $(CURDIR)/.. -shared -o $@ $^ $(LDFLAGS)
