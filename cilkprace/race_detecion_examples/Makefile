CXX = ../build/bin/clang++
CSAN_BC := $(shell find ../../../../build -name "*cilksan.bc")
PRACE_BC := $(shell find ../../../../build -name "*cilkprace.bc")
CPPFLAGS = -fopencilk -fcilktool=cilkprace -g -O3 -mllvm -csi-tool-bitcode=$(PRACE_BC)
CPPFLAGS_NOTOOL = -fopencilk -g -O3
CPPFLAGS_CSAN = -fopencilk -fsanitize=cilk -g -O3 -mllvm -csi-tool-bitcode=$(CSAN_BC)
CPPFLAGS_SAN = -fopencilk -fsanitize=cilkprace -g -O3 -mllvm -csi-tool-bitcode=$(PRACE_BC)

FILES = blah.cpp fib.cpp memcpy_race.cpp single_spawn_norace.cpp single_spawn_rr_norace.cpp stack_reuse.cpp conditional_sync.cpp  loop.cpp nqueens.cpp single_spawn_race.cpp single_spawn_rw_race.cpp # strdup.cpp

OUTS = $(patsubst %.cpp,%.out,$(FILES))
OUTS_NOTOOL = $(patsubst %.cpp,%.notool,$(FILES))
OUTS_CSAN = $(patsubst %.cpp,%.csan,$(FILES))
OUTS_SAN = $(patsubst %.cpp,%.san,$(FILES))

.PHONY: all clean

all: $(OUTS) $(OUTS_NOTOOL) $(OUTS_CSAN) $(OUTS_SAN)

%.out: %.cpp
	$(CXX) $(CPPFLAGS) $< -o $@

%.notool: %.cpp
	$(CXX) $(CPPFLAGS_NOTOOL) $< -o $@

%.csan: %.cpp
	$(CXX) $(CPPFLAGS_CSAN) $< -o $@

%.san: %.cpp
	$(CXX) $(CPPFLAGS_SAN) $< -o $@

clean:
	rm $(OUTS)
