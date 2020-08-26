CC := gcc
COMPILER_FLAGS := -std=gnu99 -O3 -g -fpic -Wall
INCLUDES := -I../util
OBJECT_FILES := nfa.o nfaCompiler.o nfaRuntime.o

LIB_NAME := grrengine

.PHONY: all clean

all: $(LIB_NAME).so $(LIB_NAME).a

$(LIB_NAME).so: $(OBJECT_FILES)
	$(CC) -shared -o $@ $^

$(LIB_NAME).a: $(OBJECT_FILES)
	ar rcs $@ $^

nfa.o: nfa.c nfaDef.h nfaInternals.h
	$(CC) $(COMPILER_FLAGS) -c $<

nfaCompiler.o: nfaCompiler.c nfaDef.h nfaInternals.h ../util/grrUtil.h
	$(CC) $(COMPILER_FLAGS) $(INCLUDES) -c $<

nfaRuntime.o: nfaRuntime.c nfaDef.h nfaInternals.h ../util/grrUtil.h
	$(CC) $(COMPILER_FLAGS) $(INCLUDES) -c $<

clean:
	rm -rf $(LIB_NAME).so $(LIB_NAME).a *.o
