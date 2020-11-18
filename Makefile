CC ?= gcc
debug ?= no

COMPILER_FLAGS := -std=gnu11 -fdiagnostics-color -Wall -Wextra
ifeq ($(debug),yes)
    COMPILER_FLAGS += -O0 -g -DDEBUG
else
    COMPILER_FLAGS += -O3 -DNDEBUG
endif

.PHONY: all clean FORCE

all: grr

grr: main.o engine/libgrrengine.a
	$(CC) $^ -o $@
	strip $@

main.o: main.c engine/include/*.h
	$(CC) $(COMPILER_FLAGS) -c $<

engine/libgrrengine.a: FORCE
	cd $(dir $@) && make CC=$(CC) debug=$(debug) $(notdir $@)

clean:
	rm -f grr *.o
	cd engine && make clean
