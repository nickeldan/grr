#ifndef __GRR_NFA_INTERNALS_H__
#define __GRR_NFA_INTERNALS_H__

#include <sys/types.h>

#include "nfaDef.h"

#define GRR_NFA_EMPTY_CHARACTER_FLAG 0x01
#define GRR_NFA_FIRST_CHAR_FLAG 0x02
#define GRR_NFA_LAST_CHAR_FLAG 0x04
#define GRR_NFA_TAB_FLAG 0x08

#define GRR_NFA_ASCII_OFFSET 4
#define GRR_NFA_NUM_SYMBOLS (GRR_NFA_ASCII_OFFSET+0x7f-0x20) // ASCII printables
#define GRR_NFA_ASCII_ADJUSTMENT (0x20-GRR_NFA_ASCII_OFFSET)

typedef struct nfaTransition {
	int motion;
	unsigned char symbols[(GRR_NFA_NUM_SYMBOLS+7)/8];
} nfaTransition;

typedef struct nfaNode {
	nfaTransition *transitions;
	unsigned int numTransitions;
} nfaNode;

struct grrNfaStruct {
	nfaNode *nodes;
	unsigned int length;
};

#endif // __GRR_NFA_INTERNALS_H__
