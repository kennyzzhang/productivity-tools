CXX = ../build/bin/clang++
CPPFLAGS = -fopencilk -fcilktool=cilkprace -g -O0

OUTS = $(patsubst %.cpp,%.out,$(wildcard *.cpp))

.PHONY: all clean

all: $(OUTS)

%.out: %.cpp
	$(CXX) $(CPPFLAGS) $< -o $@

clean:
	rm $(OUTS)
