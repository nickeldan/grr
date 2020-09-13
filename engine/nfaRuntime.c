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
    struct stateRecord *next;
    size_t startIdx;
    size_t endIdx;
    unsigned int state;
    bool ownershipFlag;
} stateRecord;

typedef struct stateSet {
    stateRecord *head;
    size_t champion;
} stateSet;

#define NEW_RECORD() calloc(1,sizeof(stateRecord))

static bool determineNextState(unsigned int depth, const grrNfa nfa, unsigned int state, char character, unsigned char flags, unsigned char *nextStateSet);
static bool canTransitionToAcceptingState(const grrNfa nfa, unsigned int state, size_t score, size_t *champion);
static int determineNextStateRecord(unsigned int depth, const grrNfa nfa, stateRecord *record, char character, unsigned char flags, stateSet *set);
static void freeStateSet(stateSet *set);

int grrMatch(const grrNfa nfa, const char *string, size_t len) {
    int ret;
    unsigned int stateSetLen;
    unsigned char flags=GRR_NFA_FIRST_CHAR_FLAG;
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
        bool stillAlive=false;
        char character;

        character=string[idx]-GRR_NFA_ASCII_ADJUSTMENT;
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
    stateSet currentSet={0}, nextSet={0};
    stateRecord *firstState;

    if ( cursor ) {
        *cursor=len;
    }

    firstState=NEW_RECORD();
    if ( !firstState ) {
        return GRR_RET_OUT_OF_MEMORY;
    }

    for (size_t idx=0; idx<len; idx++) {
        char character;
        unsigned char flags=0;

        character=string[idx];

        if ( character == '\r' || character == '\n' ) {
            if ( cursor ) {
                *cursor=idx;
            }

            break;
        }

        nextSet.head=NULL;

        if ( !isprint(character) && character != '\t' ) {
            if ( !tolerateNonprintables ) {
                if ( cursor ) {
                    *cursor=idx;
                }

                ret=GRR_RET_BAD_DATA;
                goto done;
            }

            for (stateRecord *traverse=currentSet.head; traverse;) {
                if ( traverse->state == nfa->length ) {
                    traverse=traverse->next;
                }
                else {
                    stateRecord *temp;

                    temp=traverse;
                    traverse=traverse->next;
                    free(temp);
                }
            }

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

        character -= GRR_NFA_ASCII_ADJUSTMENT;

        while ( currentSet.head ) {
            stateRecord *record;

            record=currentSet.head;
            currentSet.head=currentSet.head->next;
            record->next=NULL;
            record->ownershipFlag=0;

            ret=determineNextStateRecord(0,nfa,record,character,flags,&nextSet);
            if ( ret != GRR_RET_OK ) {
                free(record);
                goto done;
            }
            if ( !record->ownershipFlag ) {
                free(record);
            }
        }

        firstState->startIdx=firstState->endIdx=idx;
        ret=determineNextStateRecord(0,nfa,firstState,character,flags,&nextSet);
        if ( ret != GRR_RET_OK ) {
            goto done;
        }
        if ( firstState->ownershipFlag ) {
            firstState=NEW_RECORD();
            if ( !firstState ) {
                ret=GRR_RET_OUT_OF_MEMORY;
                goto done;
            }
        }
    }

    for (stateRecord *traverse=nextSet.head; traverse; traverse=traverse->next) {
        if ( canTransitionToAcceptingState(nfa,traverse->state,traverse->endIdx-traverse->startIdx,&nextSet.champion) == GRR_RET_OK ) {
            traverse->state=nfa->length;
        }
    }

    if ( nextSet.champion > 0 ) {
        for (stateRecord *traverse=nextSet.head; traverse; traverse=traverse->next) {
            if ( traverse->state == nfa->length && traverse->endIdx - traverse->startIdx == nextSet.champion ) {
                ret=GRR_RET_OK;
                *start=traverse->startIdx;
                *end=traverse->endIdx;
                goto done;
            }
        }
        assert(0);
    }
    else {
        ret=GRR_RET_NOT_FOUND;
    }

    done:

    freeStateSet(&currentSet);
    freeStateSet(&nextSet);
    free(firstState);

    return ret;
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

static bool canTransitionToAcceptingState(const grrNfa nfa, unsigned int state, size_t score, size_t *champion) {
    const nfaNode *nodes;

    if ( state == nfa->length ) {
        if ( champion && score > *champion ) {
            *champion=score;
        }

        return true;
    }

    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            unsigned int newState;

            newState=state+nodes[state].transitions[k].motion;
            if ( canTransitionToAcceptingState(nfa,newState,score,champion) ) {
                return true;
            }
        }
    }

    return false;
}

static int determineNextStateRecord(unsigned int depth, const grrNfa nfa, stateRecord *record, char character, unsigned char flags, stateSet *set) {
    unsigned int state;
    const nfaNode *nodes;

    state=record->state;

    if ( depth == 0 && record->startIdx == record->endIdx ) {
        for (stateRecord *traverse=set->head; traverse; traverse=traverse->next) {
            if ( traverse->state == state ) {
                return GRR_RET_OK;
            }
        }
    }

    if ( state == nfa->length ) { // We've reached the accepting state.
        size_t newScore;

        newScore=record->endIdx-record->startIdx;
        if ( newScore >= set->champion ) {
            record->ownershipFlag=true;
            record->next=set->head;
            set->head=record;
            set->champion=newScore;
        }
        return GRR_RET_OK;
    }

    assert(depth < nfa->length);

    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        unsigned int newState;

        newState=state+nodes[state].transitions[k].motion;

        if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,character) ) {
            stateRecord *newRecord;

            // If we're about to move into the accepting state, only keep the record if it's bigger than the current champion.
            if ( newState == nfa->length ) {
                size_t newScore;

                newScore=record->endIdx+1-record->startIdx;
                if ( newScore > set->champion ) {
                    set->champion=newScore;
                }
                else {
                    continue;
                }
            }

            if ( record->ownershipFlag ) {
                newRecord=NEW_RECORD();
                if ( !newRecord ) {
                    return GRR_RET_OUT_OF_MEMORY;
                }

                newRecord->startIdx=record->startIdx;
                newRecord->endIdx=record->endIdx;
            }
            else {
                newRecord=record;
            }

            newRecord->ownershipFlag=true;
            newRecord->state=newState;
            newRecord->endIdx++;
            newRecord->next=set->head;
            set->head=newRecord;
        }
        else if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            int ret;

            if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_FIRST_CHAR) && !(flags&GRR_NFA_FIRST_CHAR_FLAG) ) {
                continue;
            }

            if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_LAST_CHAR) && !(flags&GRR_NFA_LAST_CHAR_FLAG) ) {
                continue;
            }

            if ( record->ownershipFlag ) {
                stateRecord *newRecord;

                newRecord=NEW_RECORD();
                if ( !newRecord ) {
                    return GRR_RET_OUT_OF_MEMORY;
                }

                newRecord->startIdx=record->startIdx;
                newRecord->endIdx=record->endIdx;
                newRecord->state=newState;

                ret=determineNextStateRecord(depth+1,nfa,newRecord,character,flags,set);
                if ( !newRecord->ownershipFlag ) {
                    free(newRecord);
                }
            }
            else {
                record->state=newState;
                ret=determineNextStateRecord(depth+1,nfa,record,character,flags,set);
                if ( !record->ownershipFlag ) {
                    record->state=state;
                }
            }
            
            if ( ret != GRR_RET_OK ) {
                return ret;
            }
        }
    }

    return GRR_RET_OK;
}

static void freeStateSet(stateSet *set) {
    while ( set->head ) {
        stateRecord *temp;

        temp=set->head;
        set->head=set->head->next;
        free(temp);
    }
}
