#include <stdlib.h>
#include <stdio.h>

#include "engine.h"
#include "grrUtil.h"

typedef struct nfaTransition {
	unsigned char symbols[32];
	int motion;
} nfaTransition;

typedef struct nfaNode {
	nfaTransition *transitios;
	unsigned int numTransitions;
} nfaNode;

typedef struct nfaStackFrame {
	grrNfa *nfa;
	char reason;
} nfaStackFrame;

typedef struct nfaStack {
	nfaStackFrame *frames;
	size_t capacity;
	size_t length;
} nfaStack;

static int pushNfaToStack(nfaStack *stack, grrNfa *nfa, char reason);
static grrNfa *popParensFromStack(nfaStack *stack);

int grrCompilePattern(const char *string, size_t len, grrNfa **nfa) {
	int ret;
	nfaStack stack={0};
	grrNfa *current=NULL;

	for (size_t idx=0; idx<len; idx++) {
		switch ( string[idx] ) {
			case '(':
			ret=pushNfaToStack(&stack,current);
			if ( ret != GRR_RET_OK ) {
				goto error;
			}
			current=NULL;
			break;

			case ')':
			// 
			break;


		}
	}

	*nfa=current;

	return GRR_RET_OK;

	error:

	grrFreeNfa(current);

	return ret;
}

void grrFreeNfa(grrNfa *nfa) {

}

static int pushNfaToStack(nfaStack *stack, grrNfa *nfa, char reason) {

}