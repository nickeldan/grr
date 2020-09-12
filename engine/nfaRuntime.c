/*
Written by Daniel Walker, 2020.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>

#include "nfaRuntime.h"
#include "nfaInternals.h"

bool determineNextState(unsigned int depth, grrNfa nfa, unsigned int state, char character, unsigned char flags);
void determineNextStateRecord(unsigned int depth, grrNfa nfa, unsigned int state, char character, unsigned char flags, const nfaStateRecord *record);
bool canTransitionToAcceptingState(const grrNfa nfa, unsigned int state);

int grrMatch(grrNfa nfa, const char *string, size_t len) {
    size_t stateSetLen;
    unsigned char flags=GRR_NFA_FIRST_CHAR_FLAG;

    if ( !nfa || !string ) {
        return GRR_RET_BAD_DATA;
    }

    stateSetLen=(nfa->length+1+7)/8;

    memset(nfa->currentState.flags,0,stateSetLen);
    SET_FLAG(nfa->currentState.flags,0);

    for (size_t idx=0; idx<len; idx++) {
        bool stillAlive;
        char character;

        character=string[idx];
        if ( !isprint(character) && character != '\t' ) {
#ifdef DEBUG
            fprintf(stderr,"Nonprintable character at index %zu: 0x%02x\n", idx, (unsigned char)character);
#endif
            return GRR_RET_BAD_DATA;
        }

        character-=GRR_NFA_ASCII_ADJUSTMENT;
        memset(nfa->nextState.flags,0,stateSetLen);

        if ( idx == len-1 ) {
            flags |= GRR_NFA_LAST_CHAR_FLAG;
        }

        stillAlive=false;
        for (unsigned int state=0; state<nfa->length; state++) {
            if ( !IS_FLAG_SET(nfa->currentState.flags,state) ) {
                flags &= ~GRR_NFA_FIRST_CHAR_FLAG;
                continue;
            }

            if ( determineNextState(0,nfa,state,character,flags) ) {
                stillAlive=true;
            }
        }

        if ( !stillAlive ) {
            return GRR_RET_NOT_FOUND;
        }

        memcpy(nfa->currentState.flags,nfa->nextState.flags,stateSetLen);
        flags &= ~GRR_NFA_FIRST_CHAR_FLAG;
    }

    for (unsigned int k=0; k<nfa->length; k++) {
        if ( IS_FLAG_SET(nfa->currentState.flags,k) && canTransitionToAcceptingState(nfa,k) ) {
            return GRR_RET_OK;
        }
    }
    return GRR_RET_NOT_FOUND;
}

int grrSearch(grrNfa nfa, const char *string, size_t len, size_t *start, size_t *end, size_t *cursor, bool tolerateNonprintables) {
    size_t recordSetLen, nfaLength;

    if ( !nfa || !string ) {
        return GRR_RET_BAD_DATA;
    }

    if ( cursor ) {
        *cursor=len;
    }

    nfaLength=nfa->length;
    recordSetLen=(nfaLength+1)*sizeof(nfaStateRecord);
    memset(nfa->currentState.records,0,recordSetLen);

    for (size_t idx=0; idx<len; idx++) {
        char character;
        unsigned char flags=0;
        nfaStateRecord *temp;

        character=string[idx];

        if ( character == '\r' || character == '\n' ) {
            if ( cursor ) {
                *cursor=idx;
            }

            break;
        }

        memset(nfa->nextState.records,0,recordSetLen);

        if ( !isprint(character) && character != '\t' ) {
            if ( !tolerateNonprintables ) {
                if ( cursor ) {
                    *cursor=idx;
                }

                return GRR_RET_BAD_DATA;
            }

            memset(nfa->currentState.records,0,recordSetLen-sizeof(nfaStateRecord));

            do {
                idx++;
            } while ( idx < len && !isprint(string[idx]) && string[idx] != '\t' );

            if ( idx == len ) {
                break;
            }

            character=string[idx];
            flags |= GRR_NFA_FIRST_CHAR_FLAG;
        }
        else if ( idx == 0 ) {
            flags |= GRR_NFA_FIRST_CHAR_FLAG;
        }

        if ( idx == len-1 || !isprint(string[idx+1]) ) {
            flags |= GRR_NFA_LAST_CHAR_FLAG;
        }

        character-=GRR_NFA_ASCII_ADJUSTMENT;

        if ( nfa->currentState.records[0].score == 0 ) {
            nfa->currentState.records[0].startIdx=nfa->currentState.records[0].endIdx=idx;
        }
        determineNextStateRecord(0,nfa,0,character,flags,nfa->currentState.records+0);
        for (unsigned int k=1; k<=nfaLength; k++) {
            if ( nfa->currentState.records[k].score > 0 ) {
                determineNextStateRecord(0,nfa,k,character,flags,nfa->currentState.records+k);
            }
        }

        temp=nfa->currentState.records;
        nfa->currentState.records=nfa->nextState.records;
        nfa->nextState.records=temp;
    }

    if ( nfa->currentState.records[nfa->length].score > 0 ) {
        if ( start ) {
            *start=nfa->currentState.records[nfa->length].startIdx;
        }
        if ( end ) {
            *end=nfa->currentState.records[nfa->length].endIdx;
        }
        return GRR_RET_OK;
    }

    return GRR_RET_NOT_FOUND;
}

bool determineNextState(unsigned int depth, grrNfa nfa, unsigned int state, char character, unsigned char flags) {
    const nfaNode *nodes;
    bool stillAlive;

    assert(depth < nfa->length);

   if ( state == nfa->length ) { // We've reached the accepting state but we have another character to process.
        return false;
    }

    stillAlive=false;
    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        unsigned int newState;
        const unsigned char *symbols;

        newState=state+nodes[state].transitions[k].motion;
        symbols=nodes[state].transitions[k].symbols;

        if ( IS_FLAG_SET(symbols,character) ) {
            SET_FLAG(nfa->nextState.flags,newState);
            stillAlive=true;
        }
        else if ( IS_FLAG_SET(symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            if ( IS_FLAG_SET(symbols,GRR_NFA_FIRST_CHAR) && !(flags&GRR_NFA_FIRST_CHAR_FLAG) ) {
                continue;
            }

            if ( IS_FLAG_SET(symbols,GRR_NFA_LAST_CHAR) && !(flags&GRR_NFA_LAST_CHAR_FLAG) ) {
                continue;
            }

            if ( determineNextState(depth+1,nfa,newState,character,flags) ) {
                stillAlive=true;
            }
        }
    }

    return stillAlive;
}


void determineNextStateRecord(unsigned int depth, grrNfa nfa, unsigned int state, char character, unsigned char flags, const nfaStateRecord *record) {
    const nfaNode *nodes;
    nfaStateRecord *records;

    records=nfa->nextState.records;

    if ( state == nfa->length ) { // We're in the accepting state.
        if ( record->score > records[state].score ) {
            records[state].startIdx=record->startIdx;
            records[state].endIdx=record->endIdx;
            records[state].score=record->score;
        }

        return;
    }

    assert(depth < nfa->length);

    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        unsigned int newState;
        const unsigned char *symbols;

        newState=state+nodes[state].transitions[k].motion;
        symbols=nodes[state].transitions[k].symbols;

        if ( IS_FLAG_SET(symbols,character) ) {
            if ( record->score+1 > records[newState].score ) {
                records[newState].startIdx=record->startIdx;
                records[newState].endIdx=record->endIdx+1;
                records[newState].score=record->score+1;
            }
        }
        else if ( IS_FLAG_SET(symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            if ( IS_FLAG_SET(symbols,GRR_NFA_FIRST_CHAR) && !(flags&GRR_NFA_FIRST_CHAR_FLAG) ) {
                continue;
            }

            if ( IS_FLAG_SET(symbols,GRR_NFA_LAST_CHAR) && !(flags&GRR_NFA_LAST_CHAR_FLAG) ) {
                continue;
            }

            determineNextStateRecord(depth+1,nfa,newState,character,flags,record);
        }
    }
}

bool canTransitionToAcceptingState(const grrNfa nfa, unsigned int state) {
    const nfaNode *nodes;

    if ( state == nfa->length ) {
        return true;
    }

    nodes=nfa->nodes;

    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            unsigned int newState;

            newState=state+nodes[state].transitions[k].motion;
            if ( canTransitionToAcceptingState(nfa,newState) ) {
                return true;
            }
        }
    }

    return false;
}
