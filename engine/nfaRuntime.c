#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "nfaRuntime.h"
#include "nfaInternals.h"
#include "grrUtil.h"

static int determineNextStates(size_t depth, const grrNfa nfa, size_t state, char character, unsigned char *nextStateSet);

#define SET_FLAG(set,flag) (set)[(flag)/8] |= ( 1 << ((flag)%8) )
#define IS_FLAG_SET(set,flag) ((set)[(flag)/8]&( 1 << ((flag)%8) )

int grrNfaMatch(const grrNfa nfa, const char *string, size_t len) {
    int ret;
	size_t stateSetLen;
    unsigned char *curStateSet, *nextStateSet;

    for (size_t k=0; k<len; k++) {
        if ( !isprint(string[k]) ) {
            fprintf(stderr,"Unprintable character at index %zu: 0x%02x\n", k, (unsigned char)string[k]);
            return GRR_RET_BAD_DATA;
        }
    }

    stateSetLen=(nfa->length+1+7)/8; // The +1 is for the accepting state.
    curStateSet=calloc(1,stateSetLen);
    if ( !curStateSet ) {
        return GRR_RET_OUT_OF_MEMORY;
    }
    SET_FLAG(curStateSet,0);

    nextStateSet=malloc(stateSetLen);
    if ( !nextStateSet ) {
        free(curStateSet);
        return GRR_RET_OUT_OF_MEMORY;
    }

    for (size_t idx=0; idx<len; idx++) {
        int stillAlive=0;
        char character;
        const nfaNode *nodes;

        nodes=nfa->nodes;
        character=string[idx]-GRR_NFA_ASCII_ADJUSTMENT;
        memset(nextStateSet,0,stateSetLen);

        for (size_t state=0; state<nfa->length; state++) {
            if ( !IS_FLAG_SET(curStateSet,state) ) {
                continue;
            }

            if ( determineNextStates(0,nfa,state,character,nextStateSet) ) {
                stillAlive=1;
            }
        }

        if ( !stillAlive ) {
            ret=GRR_RET_NOT_FOUND;
            goto done;
        }

        memcpy(curStateSet,nextStateSet,stateSetLen);
    }

    done:

    free(curStateSet);
    free(nextStateSet);

    return ret;
}

int grrNfaSearch(const grrNfa nfa, const char *string, size_t len, size_t *start, size_t *end) {
	return GRR_RET_NOT_IMPLEMENTED;
}

static int determineNextStates(size_t depth, const grrNfa nfa, size_t state, char character, unsigned char *nextStateSet) {
    const nfaNode *nodes;
    int stillAlive=0;

    if ( state == nfa->length ) { // We've reached the accepting state.
        SET_FLAG(nextStateSet,state);
        return 1;
    }

    if ( depth == nfa->length ) {
        fprintf(stderr,"Something went very wrong with the construction of the NFA!  An empty-transition loop has been found at state %zu.\n", state);
        abort();
    }

    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,character) ) {
            SET_FLAG(nextStateSet,state);
            stillAlive=1;
        }
        else if ( nodes[state].transitions[k].symbols[0]&GRR_NFA_EMPTY_TRANSITION_FLAG ) {
            size_t newState;

            newState=state+nodes[state].transitions[k].motion;
            if ( determineNextStates(depth+1,maxDepth,nodes,newState,character,nextStateSet) ) {
                stillAlive=1;
            }
        }
    }

    return stillAlive;
}
