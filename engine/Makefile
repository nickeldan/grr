CC ?= gcc
debug ?= no

COMPILER_FLAGS := -std=gnu99 -fpic -Wall
ifeq ($(debug),yes)
	COMPILER_FLAGS += -O0 -g
else
	COMPILER_FLAGS += -O3
endif

OBJECT_FILES := nfa.o nfaCompiler.o nfaRuntime.o

LIB_NAME := libgrrengine

.PHONY: all clean

all: $(LIB_NAME).so $(LIB_NAME).a test

$(LIB_NAME).so: $(OBJECT_FILES)
	$(CC) -shared -o $@ $^

$(LIB_NAME).a: $(OBJECT_FILES)
	ar rcs $@ $^

test: test.o $(LIB_NAME).a
	$(CC) $^ -o $@

nfa.o: nfa.c nfaDef.h nfaInternals.h
	$(CC) $(COMPILER_FLAGS) -c $<

nfaCompiler.o: nfaCompiler.c nfaDef.h nfaInternals.h
	$(CC) $(COMPILER_FLAGS) -c $<

nfaRuntime.o: nfaRuntime.c nfaRuntime.h nfaDef.h nfaInternals.h
	$(CC) $(COMPILER_FLAGS) -c $<

test.o: test.c nfa.h nfaDef.h nfaCompiler.h nfaCompiler.c
	$(CC) $(COMPILER_FLAGS) -c $<

clean:
	rm -rf $(LIB_NAME).so $(LIB_NAME).a *.o test
