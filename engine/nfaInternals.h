#ifndef __GRR_NFA_INTERNALS_H__
#define __GRR_NFA_INTERNALS_H__

#include "nfaDef.h"

enum specialCharacterValues {
    GRR_NFA_EMPTY_TRANSITION = 0,
    GRR_NFA_FIRST_CHAR,
    GRR_NFA_LAST_CHAR,
    GRR_NFA_TAB,
};

#define GRR_NFA_EMPTY_TRANSITION_FLAG (1 << GRR_NFA_EMPTY_TRANSITION)
#define GRR_NFA_FIRST_CHAR_FLAG (1 << GRR_NFA_FIRST_CHAR)
#define GRR_NFA_LAST_CHAR_FLAG (1 << GRR_NFA_LAST_CHAR)
#define GRR_NFA_TAB_FLAG (1 << GRR_NFA_TAB)

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
    char *description;
	nfaNode *nodes;
	unsigned int length;
};

#define SET_FLAG(state,flag) (state)[(flag)/8] |= ( 1 << ((flag)%8) )
#define IS_FLAG_SET(state,flag) ( (state)[(flag)/8]&( 1 << ((flag)%8) ) )

#endif // __GRR_NFA_INTERNALS_H__
