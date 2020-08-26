#ifndef __GRR_NFA_INTERNALS_H__
#define __GRR_NFA_INTERNALS_H__

#include <sys/types.h>

#include "nfa.h"

#define GRR_NFA_ASCII_OFFSET (1+1) // wildcard + tab
#define GRR_NFA_NUM_SYMBOLS (GRR_NFA_ASCII_OFFSET+0x7f-0x20) // ASCII printables
#define GRR_NFA_ASCII_ADJUSTMENT (0x20+GR_NFA_ASCII_OFFSET)

typedef struct nfaTransition {
	ssize_t motion;
	unsigned char symbols[(GRR_NFA_NUM_SYMBOLS+7)/8];
} nfaTransition;

typedef struct nfaNode {
	nfaTransition *transitions;
	size_t length;
} nfaNode;

struct grrNfaStruct {
	nfaNode *nodes;
	size_t length;
};

#endif // __GRR_NFA_INTERNALS_H__
