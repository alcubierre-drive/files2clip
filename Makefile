CC := gcc
CFLAGS += -O3 $(shell pkg-config --cflags gtk+-3.0)
LIBS += $(shell pkg-config --libs gtk+-3.0)
SRC := $(wildcard *.c)
EXE := $(patsubst %.c,%,$(SRC))

.PHONY: all
all: $(EXE)

%: %.c Makefile
	$(CC) $(CFLAGS) $(LIBS) $< -o $@
