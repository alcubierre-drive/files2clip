CC := gcc
CFLAGS += -O3 $(shell pkg-config --cflags gtk+-3.0)
LIBS += $(shell pkg-config --libs gtk+-3.0)
SRC := $(wildcard *.c)
EXE := $(patsubst %.c,%,$(SRC))

.PHONY: all install
all: $(EXE)

%: %.c Makefile
	$(CC) $(CFLAGS) $(LIBS) $< -o $@

install: all
	mkdir -p $(PREFIX)/usr/bin/
	cp files2clip $(PREFIX)/usr/bin/
