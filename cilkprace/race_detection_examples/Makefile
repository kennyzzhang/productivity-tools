CXX = ../../../build/bin/clang++
CSAN_BC := $(shell find ../../../build -name "*cilksan*.bc")
PRACE_BC := $(shell find ../../../build -name "*cilkprace*.bc")
OPT = -O0 -v
CPPFLAGS_NOTOOL = -fopencilk -g $(OPT) -fsanitize=address -fopencilk-enable-pedigrees
CPPFLAGS_CSAN = -fopencilk -fsanitize=cilk -g $(OPT) -mllvm --enable-static-race-detection=false -mllvm -csi-tool-bitcode=$(CSAN_BC)
CPPFLAGS_SAN = -fopencilk -fsanitize=cilkprace -g $(OPT) -mllvm -csi-tool-bitcode=$(PRACE_BC)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    MAC_SDK_FLAGS = -isysroot $(shell xcrun --show-sdk-path) -L../../../build/lib/clang/21/lib/darwin
    CPPFLAGS_NOTOOL += $(MAC_SDK_FLAGS) -lopencilk-asan-personality-c_osx_dynamic -lopencilk-asan-personality-cpp_osx_dynamic
    CPPFLAGS_CSAN += $(MAC_SDK_FLAGS) -lopencilk-personality-c_osx_dynamic -lopencilk-personality-cpp_osx_dynamic
    CPPFLAGS_SAN += $(MAC_SDK_FLAGS) -lopencilk-personality-c_osx_dynamic -lopencilk-personality-cpp_osx_dynamic
endif

FILES = blah.cpp fib.cpp memcpy_race.cpp single_spawn_norace.cpp single_spawn_rr_norace.cpp stack_reuse.cpp conditional_sync.cpp  loop.cpp nqueens.cpp single_spawn_race.cpp single_spawn_rw_race.cpp funny_malloc.cpp missed_race.cpp #minimal_race.cpp # strdup.cpp

OUTS_NOTOOL = $(patsubst %.cpp,%.notool,$(FILES))
OUTS_CSAN = $(patsubst %.cpp,%.csan,$(FILES))
OUTS_SAN = $(patsubst %.cpp,%.san,$(FILES))

.PHONY: all clean test

all: $(OUTS_NOTOOL) $(OUTS_CSAN) $(OUTS_SAN)

%.notool: %.cpp
	$(CXX) $(CPPFLAGS_NOTOOL) $< -o $@

%.csan: %.cpp
	$(CXX) $(CPPFLAGS_CSAN) $< -o $@

%.san: %.cpp
	$(CXX) $(CPPFLAGS_SAN) $< -o $@

clean:
	rm $(OUTS_NOTOOL) $(OUTS_CSAN) $(OUTS_SAN)

test:
	CILK_NWORKERS=1 ./blah.san

test_race:
	CILK_NWORKERS=10 ./single_spawn_race.san

test_norace:
	CILK_NWORKERS=10 ./single_spawn_norace.san

