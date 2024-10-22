CC = ../build/bin/clang
CFLAGS = -fopencilk -fcilktool=cilkprace -g

OUTS = $(patsubst %.c,%.out,$(wildcard *.c))

.PHONY: all

all: $(OUTS)

%.out: %.c
	$(CC) $(CFLAGS) $< -o $@
