#ifndef __GRR_NFA_INTERNALS_H__
#define __GRR_NFA_INTERNALS_H__

#include <sys/types.h>

#include "nfa.h"

typedef struct nfaTransition {
	ssize_t motion;
	unsigned char symbols[32];
} nfaTransition;

typedef struct nfaNode {
	nfaTransition *transitions;
	size_t length;
	size_t capacity;
} nfaNode;

struct grrNfaStruct {
	nfaNode *nodes;
	size_t length;
	size_t capacity;
};

#endif // __GRR_NFA_INTERNALS_H__