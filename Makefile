CC ?= gcc
debug ?= no

CFLAGS := -std=gnu11 -fdiagnostics-color -Wall -Wextra
ifeq ($(debug),yes)
    CFLAGS += -O0 -g -DDEBUG
else
    CFLAGS += -O3 -DNDEBUG
endif

.PHONY: all clean FORCE

all: grr

grr: main.o engine/libgrrengine.a
	$(CC) $^ -o $@
	if [ "$(debug)" = no ]; then strip $@; fi

main.o: main.c engine/include/*.h
	$(CC) $(CFLAGS) -c $<

engine/libgrrengine.a: FORCE
	cd engine && make libgrrengine.a CC=$(CC) debug=$(debug)

clean:
	rm -f grr *.o
	cd engine && make clean
