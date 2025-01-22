CXX = ../build/bin/clang
CPPFLAGS = -fopencilk -fcilktool=cilkprace -g -O3
CPPFLAGS_NOTOOL = -fopencilk -g -O3
CPPFLAGS_CSAN = -fopencilk -fsanitize=cilk -mllvm -enable-static-race-detection=false -g -O3

OUTS = $(patsubst %.c,%.out,$(wildcard *.c))
OUTS_NOTOOL = $(patsubst %.c,%.notool,$(wildcard *.c))
OUTS_CSAN = $(patsubst %.c,%.csan,$(wildcard *.c))

.PHONY: all clean

all: $(OUTS) $(OUTS_NOTOOL) $(OUTS_CSAN)

%.out: %.c
	$(CXX) $(CPPFLAGS) $< -o $@

%.notool: %.c
	$(CXX) $(CPPFLAGS_NOTOOL) $< -o $@

%.csan: %.c
	$(CXX) $(CPPFLAGS_CSAN) $< -o $@

clean:
	rm $(OUTS)
