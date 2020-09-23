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

static bool determineNextState(unsigned int depth, const grrNfa nfa, unsigned int state, char character, unsigned char flags, unsigned char *nextStateSet);
static bool canTransitionToAcceptingState(const grrNfa nfa, unsigned int state);
static void determineNextStateRecord(unsigned int depth, grrNfa nfa, unsigned int state, const nfaStateRecord *record, char character, unsigned char flags);
static void maybePlaceRecord(const nfaStateRecord *record, unsigned int state, nfaStateSet *set, bool update_score);

int grrMatch(const grrNfa nfa, const char *string, size_t len) {
    int ret;
    unsigned int stateSetLen;
    unsigned char flags=GRR_NFA_FIRST_CHAR_FLAG;
    unsigned char *curStateSet, *nextStateSet;

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
        bool stillAlive=false;
        char character;

        character=string[idx];
        if ( !isprint(character) && character != '\t' ) {
            ret=GRR_RET_BAD_DATA;
            goto done;
        }
        character=ADJUST_CHARACTER(character);
        memset(nextStateSet,0,stateSetLen);

        if ( idx == len-1 ) {
            flags |= GRR_NFA_LAST_CHAR_FLAG;
        }

        for (unsigned int state=0; state<nfa->length; state++) {
            if ( !IS_FLAG_SET(curStateSet,state) ) {
                flags &= ~GRR_NFA_FIRST_CHAR_FLAG;
                continue;
            }

            if ( determineNextState(0,nfa,state,character,flags,nextStateSet) ) {
                stillAlive=true;
            }
        }

        if ( !stillAlive ) {
            ret=GRR_RET_NOT_FOUND;
            goto done;
        }

        memcpy(curStateSet,nextStateSet,stateSetLen);
        flags &= ~GRR_NFA_FIRST_CHAR_FLAG;
    }

    for (unsigned int k=0; k<nfa->length; k++) {
        if ( IS_FLAG_SET(curStateSet,k) && canTransitionToAcceptingState(nfa,k) ) {
            ret=GRR_RET_OK;
            goto done;
        }
    }
    ret=GRR_RET_NOT_FOUND;

    done:

    free(curStateSet);
    free(nextStateSet);

    return ret;
}

int grrSearch(grrNfa nfa, const char *string, size_t len, size_t *start, size_t *end, size_t *cursor, bool tolerateNonprintables) {
    unsigned int length;

    length=nfa->length;
    nfa->current.length=0;

    if ( cursor ) {
        *cursor=len;
    }

    for (size_t idx=0; idx<len; idx++) {
        char character;
        unsigned char flags=0;
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
                if ( nfa->current.records[k].state == length ) {
                    if ( k > 0 ) {
                        memcpy(nfa->current.records+0,nfa->current.records+k,sizeof(nfa->current.records[0]));
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
            determineNextStateRecord(0,nfa,nfa->current.records[k].state,nfa->current.records+k,character,flags);
        }

        firstState.state=0;
        firstState.startIdx=firstState.endIdx=idx;
        firstState.score=0;

        determineNextStateRecord(0,nfa,0,&firstState,character,flags);

        memcpy(nfa->current.records,nfa->next.records,nfa->next.length*sizeof(*nfa->next.records));
        nfa->current.length=nfa->next.length;
    }

    for (unsigned int k=0; k<nfa->current.length; k++) {
        if ( nfa->current.records[k].state == length ) {
            if ( start ) {
                *start=nfa->current.records[k].startIdx;
            }
            if ( end ) {
                *end=nfa->current.records[k].endIdx;
            }

            return GRR_RET_OK;
        }
    }

    return GRR_RET_NOT_FOUND;
}

static bool determineNextState(unsigned int depth, const grrNfa nfa, unsigned int state, char character, unsigned char flags, unsigned char *nextStateSet) {
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
            SET_FLAG(nextStateSet,newState);
            stillAlive=true;
        }
        else if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_FIRST_CHAR) && !(flags&GRR_NFA_FIRST_CHAR_FLAG) ) {
                continue;
            }

            if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_LAST_CHAR) && !(flags&GRR_NFA_LAST_CHAR_FLAG) ) {
                continue;
            }

            if ( determineNextState(depth+1,nfa,newState,character,flags,nextStateSet) ) {
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
        if ( set->records[k].state == state ) {
            break;
        }
    }

    newScore=record->score+(update_score? 1 : 0);
    if ( k == set->length || newScore > set->records[k].score ) {
        set->records[k].startIdx=record->startIdx;
        set->records[k].endIdx=record->endIdx;
        set->records[k].score=newScore;
        if ( update_score ) {
            set->records[k].endIdx++;
        }

        if ( k == set->length ) {
            set->records[k].state=state;
            set->length++;
        }
    }
}
