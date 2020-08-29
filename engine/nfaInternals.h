#ifndef __GRR_NFA_INTERNALS_H__
#define __GRR_NFA_INTERNALS_H__

#include "nfaDef.h"

#define GRR_NFA_EMPTY_TRANSITION_FLAG 0x01
#define GRR_NFA_FIRST_CHAR_FLAG 0x02
#define GRR_NFA_LAST_CHAR_FLAG 0x04
#define GRR_NFA_TAB_FLAG 0x08
#define GRR_NFA_TAB_REPRESENTATION 4

#define GRR_NFA_ASCII_OFFSET 4
#define GRR_NFA_NUM_SYMBOLS (GRR_NFA_ASCII_OFFSET + 0x7e + 1 - 0x20) // ASCII printables
#define GRR_NFA_ASCII_ADJUSTMENT (0x20 - GRR_NFA_ASCII_OFFSET)

typedef struct nfaTransition {
	int motion;
	unsigned char symbols[(GRR_NFA_NUM_SYMBOLS+7)/8];
} nfaTransition;

typedef struct nfaNode {
	nfaTransition transitions[2];
	unsigned char twoTransitions;
} nfaNode;

struct grrNfaStruct {
	nfaNode *nodes;
	unsigned int length;
};

#define SET_FLAG(state,flag) (state)[(flag)/8] |= ( 1 << ((flag)%8) )
#define IS_FLAG_SET(state,flag) ( (state)[(flag)/8]&( 1 << ((flag)%8) ) )

#endif // __GRR_NFA_INTERNALS_H__
