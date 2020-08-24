CC := gcc
COMPILER_FLAGS := -std=gnu99 -O3 -g -fpic -Wall
INCLUDES := -I../util

LIB_NAME := grrengine

.PHONY: all clean

all: $(LIB_NAME).so $(LIB_NAME).a

$(LIB_NAME).so: compiler.o nfa.o
	$(CC) -shared -o $@ $^

$(LIB_NAME).a: compiler.o nfa.o
	ar rcs $@ $^

compiler.o: compiler.c compiler.h nfa.h nfaInternals.h ../util/grrUtil.h
	$(CC) $(COMPILER_FLAGS) $(INCLUDES) -c $<

nfa.o: nfa.c nfa.h nfaInternals.h
	$(CC) $(COMPILER_FLAGS) -c $<

clean:
	rm -rf $(LIB_NAME).so $(LIB_NAME).a *.o
