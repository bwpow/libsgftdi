GCC ?= gcc
GPP ?= g++
AR ?= ar
RM ?= rm
CP ?= cp
MKDIR ?= @mkdir -p

DESTINCLUDE ?= /usr/local/include/
DESTLIB ?= /usr/local/lib/

ST_LIBS = -pie -lrt
ST_CPPFLAGS = -pipe -fPIE -Wall -Wextra -Wshadow -fstack-protector-strong -O3 -std=c++17 -march=native -I$(DESTINCLUDE)
ST_CFLAGS = -pipe -fPIE -Wshadow -fstack-protector-strong -O3 -std=c11 -march=native -I$(DESTINCLUDE)

MT_LIBS = -pthread $(ST_LIBS)
MT_CPPFLAGS = -pthread $(ST_CPPFLAGS)
MT_CFLAGS = -pthread $(ST_CFLAGS)

ifdef SHAGA_SANITY
	SANITY = -fsanitize=address -fsanitize=undefined -fsanitize=leak -fno-omit-frame-pointer
	ST_LIBS += $(SANITY)
	ST_CPPFLAGS += $(SANITY)
	ST_CFLAGS += $(SANITY)
	MT_LIBS += $(SANITY)
	MT_CPPFLAGS += $(SANITY)
	MT_CFLAGS += $(SANITY)
endif

LIBDIR = lib
OBJDIR = obj
BINDIR = bin

BINEXT = bin

include Makefile.in
