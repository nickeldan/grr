/*
Written by Daniel Walker, 2020.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "nfaRuntime.h"
#include "nfaInternals.h"

static bool determineNextState(unsigned int depth, const grrNfa nfa, unsigned int state, char character, unsigned char flags);
static bool canTransitionToAcceptingState(const grrNfa nfa, unsigned int state);
static void determineNextStateRecord(unsigned int depth, grrNfa nfa, unsigned int state, const nfaStateRecord *record, char character, unsigned char flags);
static void maybePlaceRecord(const nfaStateRecord *record, unsigned int state, nfaStateSet *set, bool update_score);

int grrMatch(const grrNfa nfa, const char *string, size_t len) {
    unsigned int stateSetLen;
    unsigned char flags=GRR_NFA_FIRST_CHAR_FLAG;

    stateSetLen=(nfa->length+1+7)/8; // The +1 is for the accepting state.
    memset(nfa->current.s_flags,0,stateSetLen);
    SET_FLAG(nfa->current.s_flags,0);

    for (size_t idx=0; idx<len; idx++) {
        bool stillAlive=false;
        char character;

        character=string[idx];
        if ( !isprint(character) && character != '\t' ) {
            return GRR_RET_BAD_DATA;
        }

        character=ADJUST_CHARACTER(character);
        memset(nfa->next.s_flags,0,stateSetLen);

        if ( idx == len-1 ) {
            flags |= GRR_NFA_LAST_CHAR_FLAG;
        }

        for (unsigned int state=0; state<nfa->length; state++) {
            if ( !IS_FLAG_SET(nfa->current.s_flags,state) ) {
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

        memcpy(nfa->current.s_flags,nfa->next.s_flags,stateSetLen);
        flags &= ~GRR_NFA_FIRST_CHAR_FLAG;
    }

    for (unsigned int k=0; k<nfa->length; k++) {
        if ( IS_FLAG_SET(nfa->current.s_flags,k) && canTransitionToAcceptingState(nfa,k) ) {
            return GRR_RET_OK;
        }
    }

    return GRR_RET_NOT_FOUND;
}

int grrSearch(grrNfa nfa, const char *string, size_t len, size_t *start, size_t *end, size_t *cursor, bool tolerateNonprintables) {
    unsigned int length;
    unsigned char flags=GRR_NFA_FIRST_CHAR_FLAG;

    length=nfa->length;
    nfa->current.length=0;

    if ( cursor ) {
        *cursor=len;
    }

    for (size_t idx=0; idx<len; idx++) {
        char character;
        unsigned int currentLength;
        nfaStateRecord firstState;

        character=string[idx];

        if ( character == '\r' || character == '\n' ) {
            if ( cursor ) {
                *cursor=idx;
            }

            break;
        }

        nfa->next.length=0;
        currentLength=nfa->current.length;

        if ( !isprint(character) && character != '\t' ) {
            if ( !tolerateNonprintables ) {
                if ( cursor ) {
                    *cursor=idx;
                }

                return GRR_RET_BAD_DATA;
            }

            for (unsigned int k=0; k<currentLength; k++) {
                if ( nfa->current.s_records[k].state == length ) {
                    if ( k > 0 ) {
                        memcpy(nfa->current.s_records+0,nfa->current.s_records+k,sizeof(nfa->current.s_records[0]));
                    }
                    currentLength=nfa->current.length=1;
                    goto skip_over_clear;
                }
            }

            memset(&nfa->current,0,sizeof(nfa->current));

            skip_over_clear:
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

        character=ADJUST_CHARACTER(character);

        for (unsigned int k=0; k<currentLength; k++) {
            determineNextStateRecord(0,nfa,nfa->current.s_records[k].state,nfa->current.s_records+k,character,flags);
        }

        firstState.state=0;
        firstState.startIdx=firstState.endIdx=idx;
        firstState.score=0;

        determineNextStateRecord(0,nfa,0,&firstState,character,flags);

        memcpy(nfa->current.s_records,nfa->next.s_records,nfa->next.length*sizeof(*nfa->next.s_records));
        nfa->current.length=nfa->next.length;

        flags &= ~GRR_NFA_FIRST_CHAR_FLAG;
    }

    for (unsigned int k=0; k<nfa->current.length; k++) {
        if ( nfa->current.s_records[k].state == length ) {
            if ( start ) {
                *start=nfa->current.s_records[k].startIdx;
            }
            if ( end ) {
                *end=nfa->current.s_records[k].endIdx;
            }

            return GRR_RET_OK;
        }
    }

    return GRR_RET_NOT_FOUND;
}

static bool determineNextState(unsigned int depth, const grrNfa nfa, unsigned int state, char character, unsigned char flags) {
    const nfaNode *nodes;
    bool stillAlive=false;

    if ( state == nfa->length ) { // We've reached the accepting state but we have another character to process.
        return false;
    }

    assert(depth < nfa->length);

    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        unsigned int newState;

        newState=state+nodes[state].transitions[k].motion;

        if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,character) ) {
            SET_FLAG(nfa->next.s_flags,newState);
            stillAlive=true;
        }
        else if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_FIRST_CHAR) && !(flags&GRR_NFA_FIRST_CHAR_FLAG) ) {
                continue;
            }

            if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_LAST_CHAR) && !(flags&GRR_NFA_LAST_CHAR_FLAG) ) {
                continue;
            }

            if ( determineNextState(depth+1,nfa,newState,character,flags) ) {
                stillAlive=true;
            }
        }
    }

    return stillAlive;
}

static bool canTransitionToAcceptingState(const grrNfa nfa, unsigned int state) {
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

static void determineNextStateRecord(unsigned int depth, grrNfa nfa, unsigned int state, const nfaStateRecord *record, char character, unsigned char flags) {
    const nfaNode *nodes;

    if ( state == nfa->length ) {
        maybePlaceRecord(record,state,&nfa->next,(depth > 0));
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
            maybePlaceRecord(record,newState,&nfa->next,true);
        }
        else if ( IS_FLAG_SET(symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            if ( IS_FLAG_SET(symbols,GRR_NFA_FIRST_CHAR) && !(flags&GRR_NFA_FIRST_CHAR_FLAG) ) {
                continue;
            }

            if ( IS_FLAG_SET(symbols,GRR_NFA_LAST_CHAR) && !(flags&GRR_NFA_LAST_CHAR_FLAG) ) {
                continue;
            }

            determineNextStateRecord(depth+1,nfa,newState,record,character,flags);
        }
    }
}

static void maybePlaceRecord(const nfaStateRecord *record, unsigned int state, nfaStateSet *set, bool update_score) {
    unsigned int k;
    size_t newScore;

    for (k=0; k<set->length; k++) {
        if ( set->s_records[k].state == state ) {
            break;
        }
    }

    newScore=record->score+(update_score? 1 : 0);
    if ( k == set->length || newScore > set->s_records[k].score ) {
        set->s_records[k].startIdx=record->startIdx;
        set->s_records[k].endIdx=record->endIdx;
        set->s_records[k].score=newScore;
        if ( update_score ) {
            set->s_records[k].endIdx++;
        }

        if ( k == set->length ) {
            set->s_records[k].state=state;
            set->length++;
        }
    }
}
