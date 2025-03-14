GCC ?= gcc
GPP ?= g++
AR ?= ar
RM ?= rm -f
CP ?= cp
MKDIR ?= @mkdir -p
STRIP ?= strip --strip-unneeded --preserve-dates

SHAGA_MARCH ?= native

DESTINCLUDE ?= /usr/local/include/
DESTLIB ?= /usr/local/lib/

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
	-freorder-blocks-algorithm=simple \
	-O3 \
	-std=c++17 \
	-march=$(SHAGA_MARCH) \
	-mtune=$(SHAGA_MARCH) \
	-I$(DESTINCLUDE)

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
	-freorder-blocks-algorithm=simple \
	-O3 \
	-std=c11 \
	-march=$(SHAGA_MARCH) \
	-mtune=$(SHAGA_MARCH) \
	-I$(DESTINCLUDE)

# Detect compiler type and version
COMPILER_ID := $(shell $(GPP) -v 2>&1 | grep -q "gcc version" && echo "GCC" || ($(GPP) -v 2>&1 | grep -q "clang version" && echo "CLANG" || echo "OTHER"))

# If GCC, check version
ifeq ($(COMPILER_ID),GCC)
	GCC_VERSION := $(shell $(GPP) -dumpversion)
	GCC_VERSION_GE9 := $(shell echo "$(GCC_VERSION) >= 9" | bc)

	# Add -flto=auto for GCC 9 or higher
	ifeq ($(GCC_VERSION_GE9),1)
		ST_CPPFLAGS += -flto=auto
	endif
else
	# For Clang, MSVC, or any other compiler, always add -flto=auto
	ST_CPPFLAGS += -flto=auto
endif

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
