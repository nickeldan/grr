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
	if [ "$(debug)" = no ]; then strip $@; fi

main.o: main.c engine/include/*.h
	$(CC) $(COMPILER_FLAGS) -c $<

engine/libgrrengine.a: FORCE
	cd engine && make libgrrengine.a CC=$(CC) debug=$(debug)

clean:
	rm -f grr *.o
	cd engine && make clean
