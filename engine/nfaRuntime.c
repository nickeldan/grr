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

typedef struct stateRecord {
    size_t startIdx;
    size_t endIdx;
    size_t score;
} stateRecord;

static int determineNextState(size_t depth, const grrNfa nfa, size_t state, char character, unsigned char flags, unsigned char *nextStateSet);
static int determineNextStateRecord(size_t depth, const grrNfa nfa, size_t state, char character, unsigned char flags, const stateRecord *record, stateRecord **records);
static int canTransitionToAcceptingState(const grrNfa nfa, size_t state, size_t score, size_t *champion);

int grrMatch(const grrNfa nfa, const char *string, size_t len) {
    int ret;
    size_t stateSetLen;
    unsigned char flags=GRR_NFA_FIRST_CHAR_FLAG;
    unsigned char *curStateSet, *nextStateSet;

    if ( !nfa || !string ) {
        return GRR_RET_BAD_DATA;
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

        character=string[idx];
        if ( !isprint(character) ) {
#ifdef DEBUG
            fprintf(stderr,"Nonprintable character at index %zu: 0x%02x\n", k, (unsigned char)character);
#endif
            ret=GRR_RET_BAD_DATA;
            goto done;
        }

        character-=GRR_NFA_ASCII_ADJUSTMENT;
        memset(nextStateSet,0,stateSetLen);

        if ( idx == len-1 ) {
            flags |= GRR_NFA_LAST_CHAR_FLAG;
        }

        for (size_t state=0; state<nfa->length; state++) {
            if ( !IS_FLAG_SET(curStateSet,state) ) {
                flags &= ~GRR_NFA_FIRST_CHAR_FLAG;
                continue;
            }

            if ( determineNextState(0,nfa,state,character,flags,nextStateSet) ) {
                stillAlive=1;
            }
        }

        if ( !stillAlive ) {
            ret=GRR_RET_NOT_FOUND;
            goto done;
        }

        memcpy(curStateSet,nextStateSet,stateSetLen);
        flags &= ~GRR_NFA_FIRST_CHAR_FLAG;
    }

    for (size_t k=0; k<nfa->length; k++) {
        if ( IS_FLAG_SET(curStateSet,k) && canTransitionToAcceptingState(nfa,k,0,NULL) == GRR_RET_OK ) {
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

int grrSearch(const grrNfa nfa, const char *string, size_t len, size_t *start, size_t *end, size_t *cursor, bool tolerateNonprintables) {
    int ret;
    stateRecord *currentRecords=NULL, *nextRecords=NULL;

    if ( !nfa || !string ) {
        return GRR_RET_BAD_DATA;
    }

    if ( cursor ) {
        *cursor=len;
    }

    for (size_t idx=0; idx<len; idx++) {
        char character;
        unsigned char flags=0;
        stateRecord *temp;

        character=string[idx];

        if ( character == '\r' || character == '\n' ) {
            if ( cursor ) {
                *cursor=idx;
            }

            break;
        }

        if ( nextRecords ) {
            memset(nextRecords,0,(nfa->length+1)*sizeof(*nextRecords));
        }

        if ( !isprint(character) ) {
            if ( !tolerateNonprintables ) {
                if ( cursor ) {
                    *cursor=idx;
                }

                ret=GRR_RET_BAD_DATA;
                goto done;
            }

            if ( currentRecords ) {
                memset(currentRecords,0,nfa->length*sizeof(*currentRecords));
            }

            do {
                idx++;
            } while ( idx < len && !isprint(string[idx]) );

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

        if ( currentRecords ) {
            if ( currentRecords[0].score == 0 ) {
                currentRecords[0].startIdx=currentRecords[0].endIdx=idx;
            }
            ret=determineNextStateRecord(0,nfa,0,character,flags,currentRecords+0,&nextRecords);
            if ( ret != GRR_RET_OK ) {
                goto done;
            }
            for (size_t k=1; k<=nfa->length; k++) {
                if ( currentRecords[k].score > 0 ) {
                    ret=determineNextStateRecord(0,nfa,k,character,flags,currentRecords+k,&nextRecords);
                    if ( ret != GRR_RET_OK ) {
                        goto done;
                    }
                }
            }
        }
        else {
            stateRecord firstState;

            firstState.startIdx=firstState.endIdx=idx;
            firstState.score=0;

            ret=determineNextStateRecord(0,nfa,0,character,flags,&firstState,&nextRecords);
            if ( ret != GRR_RET_OK ) {
                goto done;
            }
        }

        temp=currentRecords;
        currentRecords=nextRecords;
        nextRecords=temp;
    }

    if ( currentRecords && currentRecords[nfa->length].score > 0 ) {
        if ( start ) {
            *start=currentRecords[nfa->length].startIdx;
        }
        if ( end ) {
            *end=currentRecords[nfa->length].endIdx;
        }
        ret=GRR_RET_OK;
    }
    else {
        ret=GRR_RET_NOT_FOUND;
    }

    done:

    free(currentRecords);
    free(nextRecords);

    return ret;
}

static int determineNextState(size_t depth, const grrNfa nfa, size_t state, char character, unsigned char flags, unsigned char *nextStateSet) {
    const nfaNode *nodes;
    int stillAlive=0;

    assert(depth < nfa->length);

   if ( state == nfa->length ) { // We've reached the accepting state but we have another character to process.
        return 0;
    }

    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        size_t newState;
        const unsigned char *symbols;

        newState=state+nodes[state].transitions[k].motion;
        symbols=nodes[state].transitions[k].symbols;

        if ( IS_FLAG_SET(symbols,character) ) {
            SET_FLAG(nextStateSet,newState);
            stillAlive=1;
        }
        else if ( IS_FLAG_SET(symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            if ( IS_FLAG_SET(symbols,GRR_NFA_FIRST_CHAR) && !(flags&GRR_NFA_FIRST_CHAR_FLAG) ) {
                continue;
            }

            if ( IS_FLAG_SET(symbols,GRR_NFA_LAST_CHAR) && !(flags&GRR_NFA_LAST_CHAR_FLAG) ) {
                continue;
            }

            if ( determineNextState(depth+1,nfa,newState,character,flags,nextStateSet) ) {
                stillAlive=1;
            }
        }
    }

    return stillAlive;
}


static int determineNextStateRecord(size_t depth, const grrNfa nfa, size_t state, char character, unsigned char flags, const stateRecord *record, stateRecord **records) {
    int ret;
    const nfaNode *nodes;

    if ( state == nfa->length ) { // We're in the accepting state.
        if ( !*records ) {
            *records=calloc(nfa->length+1,sizeof(**records));
            if ( !*records ) {
                return GRR_RET_OUT_OF_MEMORY;
            }
        }
        if ( record->score > (*records)[state].score ) {
            (*records)[state].startIdx=record->startIdx;
            (*records)[state].endIdx=record->endIdx;
            (*records)[state].score=record->score;
        }

        return GRR_RET_OK;
    }

    assert(depth < nfa->length);

    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        size_t newState;
        const unsigned char *symbols;

        newState=state+nodes[state].transitions[k].motion;
        symbols=nodes[state].transitions[k].symbols;

        if ( IS_FLAG_SET(symbols,character) ) {
            if ( !*records ) {
                *records=calloc(nfa->length+1,sizeof(**records));
                if ( !*records ) {
                    return GRR_RET_OUT_OF_MEMORY;
                }
            }

            if ( record->score+1 > (*records)[newState].score ) {
                (*records)[newState].startIdx=record->startIdx;
                (*records)[newState].endIdx=record->endIdx+1;
                (*records)[newState].score=record->score+1;
            }
        }
        else if ( IS_FLAG_SET(symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            if ( IS_FLAG_SET(symbols,GRR_NFA_FIRST_CHAR) && !(flags&GRR_NFA_FIRST_CHAR_FLAG) ) {
                continue;
            }

            if ( IS_FLAG_SET(symbols,GRR_NFA_LAST_CHAR) && !(flags&GRR_NFA_LAST_CHAR_FLAG) ) {
                continue;
            }

            ret=determineNextStateRecord(depth+1,nfa,newState,character,flags,record,records);
            if ( ret != GRR_RET_OK ) {
                return ret;
            }
        }
    }

    return GRR_RET_OK;
}

static int canTransitionToAcceptingState(const grrNfa nfa, size_t state, size_t score, size_t *champion) {
    const nfaNode *nodes;

    if ( state == nfa->length ) {
        if ( champion && score > *champion ) {
            *champion=score;
        }

        return GRR_RET_OK;
    }

    nodes=nfa->nodes;

    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            size_t newState;

            newState=state+nodes[state].transitions[k].motion;
            if ( canTransitionToAcceptingState(nfa,newState,score,champion) == GRR_RET_OK ) {
                return GRR_RET_OK;
            }
        }
    }

    return GRR_RET_NOT_FOUND;
}
