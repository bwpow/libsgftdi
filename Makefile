CROSS_COMPILE ?=
GCC ?= $(CROSS_COMPILE)gcc
GPP ?= $(CROSS_COMPILE)g++
AR ?= $(CROSS_COMPILE)ar
RM ?= rm -f
CP ?= cp
MKDIR ?= @mkdir -p
STRIP ?= $(CROSS_COMPILE)strip --strip-unneeded --preserve-dates

SHAGA_MARCH ?= native
SHAGA_MTUNE ?= native

DESTINCLUDE ?= /usr/local/include/
DESTLIB ?= /usr/local/lib/

ADDITIONAL_CFLAGS ?=
ADDITIONAL_CPPFLAGS ?=

ST_LIBS = \
	-pie \
	-lrt

ST_CPPFLAGS = \
	-pipe \
	-fPIE \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wduplicated-cond \
	-Wduplicated-branches \
	-Wlogical-op \
	-Wrestrict \
	-Wnull-dereference \
	-Wdouble-promotion \
	-Wno-unknown-warning-option \
	-fstack-protector-strong \
	-fsized-deallocation \
	-fwrapv \
	-O3 \
	-std=c++17 \
	-march=$(SHAGA_MARCH) \
	-mtune=$(SHAGA_MTUNE) \
	-I$(DESTINCLUDE) \
	$(ADDITIONAL_CPPFLAGS)

ST_CFLAGS = \
	-pipe \
	-fPIE \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wduplicated-cond \
	-Wduplicated-branches \
	-Wlogical-op \
	-Wrestrict \
	-Wnull-dereference \
	-Wdouble-promotion \
	-Wno-unknown-warning-option \
	-fstack-protector-strong \
	-fwrapv \
	-O3 \
	-std=c11 \
	-march=$(SHAGA_MARCH) \
	-mtune=$(SHAGA_MTUNE) \
	-I$(DESTINCLUDE) \
	$(ADDITIONAL_CFLAGS)

ifdef SHAGA_SANITY
	SANITY = -fsanitize=address -fsanitize=undefined -fsanitize=leak -fsanitize-address-use-after-scope -fno-omit-frame-pointer
	ST_LIBS += $(SANITY)
	ST_CPPFLAGS += $(SANITY)
	ST_CFLAGS += $(SANITY)
endif

MT_LIBS = -pthread $(ST_LIBS) -latomic
MT_CPPFLAGS = -pthread $(ST_CPPFLAGS)
MT_CFLAGS = -pthread $(ST_CFLAGS)

LIBDIR = lib
OBJDIR = obj
BINDIR = bin

BINEXT = bin

include Makefile.in
