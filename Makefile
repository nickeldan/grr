CC ?= gcc
debug ?= no

COMPILER_FLAGS := -std=gnu11 -Wall
ifeq ($(debug),yes)
    COMPILER_FLAGS += -O0 -g -DDEBUG
else
    COMPILER_FLAGS += -O3 -DNDEBUG
endif

.PHONY: all clean FORCE

all: grr

grr: main.o engine/libgrrengine.a
	$(CC) $^ -o $@

main.o: main.c engine/nfa.h engine/nfaDef.h engine/nfaCompiler.h engine/nfaRuntime.h
	$(CC) $(COMPILER_FLAGS) $(INCLUDES) -c $<

engine/libgrrengine.a: FORCE
	cd $(dir $@) && make CC=$(CC) debug=$(debug) $(notdir $@)

clean:
	rm -f grr *.o
	cd engine && make clean
