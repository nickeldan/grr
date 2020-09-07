CC ?= gcc
debug ?= no

COMPILER_FLAGS := -std=gnu99 -Wall
ifeq ($(debug),yes)
    COMPILER_FLAGS += -O0 -g
else
    COMPILER_FLAGS += -O3
endif

INCLUDES := -I../engine

.PHONY: all clean ../engine/libgrrengine.a

all: grr

grr: main.o ../engine/libgrrengine.a
	$(CC) $^ -o $@

main.o: main.c ../engine/nfa.h ../engine/nfaDef.h ../engine/nfaCompiler.h ../engine/nfaRuntime.h
	$(CC) $(COMPILER_FLAGS) $(INCLUDES) -c $<

../engine/libgrrengine.a:
	cd $(dir $@) && make $(notdir $@)

clean:
	rm -r grr *.o
