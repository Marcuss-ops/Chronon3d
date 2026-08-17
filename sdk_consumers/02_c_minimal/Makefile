# 02_c_minimal — build the C ABI consumer against the installed package.
#
# Usage:
#   make            # build c_minimal
#   make run        # build + run (sets LD_LIBRARY_PATH)
#   make clean
#
# The install prefix is taken from SDK_PREFIX / CHRONON3D_PREFIX, falling back
# to /usr/local.  pkg-config (chronon3d.pc) is preferred when discoverable;
# otherwise explicit prefix-based flags are used.

PREFIX   ?= $(SDK_PREFIX)
ifndef PREFIX
PREFIX   := $(CHRONON3D_PREFIX)
endif
ifndef PREFIX
PREFIX   := /usr/local
endif

PKGCONF  ?= pkg-config
CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra

HAVE_PC := $(shell $(PKGCONF) --exists chronon3d 2>/dev/null && echo 1)
ifeq ($(HAVE_PC),1)
CPPFLAGS += $(shell $(PKGCONF) --cflags chronon3d)
LDLIBS   += $(shell $(PKGCONF) --libs chronon3d)
else
CPPFLAGS += -I$(PREFIX)/include
LDLIBS   += -L$(PREFIX)/lib -lchronon3d_c
endif

c_minimal: main.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ main.c $(LDLIBS)

run: c_minimal
	LD_LIBRARY_PATH="$(PREFIX)/lib$${LD_LIBRARY_PATH:+:$${LD_LIBRARY_PATH}}" ./c_minimal

clean:
	rm -f c_minimal

.PHONY: run clean
