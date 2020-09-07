#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "nfaRuntime.h"
#include "nfaInternals.h"

typedef struct stateRecord {
    struct stateRecord *next;
    size_t state;
    size_t startIdx;
    size_t endIdx;
    unsigned char ownershipFlag;
} stateRecord;

typedef struct stateSet {
    stateRecord *head;
    size_t champion;
} stateSet;

#define NEW_RECORD() calloc(1,sizeof(stateRecord))

static int determineNextState(size_t depth, const grrNfa nfa, size_t state, char character, unsigned char flags, unsigned char *nextStateSet);
static int canTransitionToAcceptingState(const grrNfa nfa, size_t state, size_t score, size_t *champion);
static int determineNextStateRecord(size_t depth, const grrNfa nfa, stateRecord *record, char character, unsigned char flags, stateSet *set);
static void freeStateSet(stateSet *set);

int grrMatch(const grrNfa nfa, const char *string, size_t len) {
    int ret;
    size_t stateSetLen;
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
        int stillAlive=0;
        char character;

        character=string[idx]-GRR_NFA_ASCII_ADJUSTMENT;
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

int grrSearch(const grrNfa nfa, const char *string, size_t len, size_t *start, size_t *end) {
    int ret;
    stateSet currentSet={0}, nextSet={0};
    stateRecord *firstState;

    firstState=NEW_RECORD();
    if ( !firstState ) {
        return GRR_RET_OUT_OF_MEMORY;
    }

    for (size_t idx=0; idx<len; idx++) {
        char character;
        unsigned char flags=0;

        memcpy(&currentSet,&nextSet,sizeof(nextSet));
        nextSet.head=NULL;

        while ( !isprint(string[idx]) && idx < len ) {
            stateRecord *prev=NULL;

            for (stateRecord *traverse=currentSet.head; traverse;) {
                if ( traverse->state < nfa->length ) {
                    stateRecord *temp;

                    temp=traverse;

                    if ( prev ) {
                        traverse=prev->next=traverse->next;
                    }
                    else {
                        traverse=currentSet.head=traverse->next;
                    }

                    free(temp);
                }
                else {
                    prev=traverse;
                    traverse=traverse->next;
                }
            }

            flags |= GRR_NFA_FIRST_CHAR_FLAG;
            idx++;
        }
        if ( idx == len ) {
            break;
        }

        if ( idx == 0 ) {
            flags |= GRR_NFA_FIRST_CHAR_FLAG;
        }
        if ( idx == len-1 || !isprint(string[idx+1]) ) {
            flags |= GRR_NFA_LAST_CHAR_FLAG;
        }

        character=string[idx]-GRR_NFA_ASCII_ADJUSTMENT;

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
                *end=traverse->endIdx-1;
                goto done;
            }
        }
        fprintf(stderr,"Something went really wrong with the search!  The state record containing the match was somehow deleted.\n");
        abort();
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

static int determineNextState(size_t depth, const grrNfa nfa, size_t state, char character, unsigned char flags, unsigned char *nextStateSet) {
    const nfaNode *nodes;
    int stillAlive=0;

    if ( state == nfa->length ) { // We've reached the accepting state but we have another character to process.
        return 0;
    }

    if ( depth == nfa->length ) {
        if ( character == GRR_NFA_TAB ) {
            fprintf(stderr,"Something went very wrong with the construction of the NFA!  An empty-transition loop has been found at state %zu while processing a '\\t'.\n", state);
        }
        else {
            fprintf(stderr,"Something went very wrong with the construction of the NFA!  An empty-transition loop has been found at state %zu while processing a '%c'.\n", state, character+GRR_NFA_ASCII_ADJUSTMENT);
        }
        abort();
    }

    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        size_t newState;

        newState=state+nodes[state].transitions[k].motion;

        if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,character) ) {
            SET_FLAG(nextStateSet,newState);
            stillAlive=1;
        }
        else if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_EMPTY_TRANSITION) ) {
            if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_FIRST_CHAR) && !(flags&GRR_NFA_FIRST_CHAR_FLAG) ) {
                continue;
            }

            if ( IS_FLAG_SET(nodes[state].transitions[k].symbols,GRR_NFA_LAST_CHAR) && !(flags&GRR_NFA_LAST_CHAR_FLAG) ) {
                continue;
            }

            if ( determineNextState(depth+1,nfa,newState,character,flags,nextStateSet) ) {
                stillAlive=1;
            }
        }
    }

    return stillAlive;
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

static int determineNextStateRecord(size_t depth, const grrNfa nfa, stateRecord *record, char character, unsigned char flags, stateSet *set) {
    size_t state;
    const nfaNode *nodes;

    state=record->state;

    if ( state == nfa->length ) { // We've reached the accepting state.
        size_t newScore;

        newScore=record->endIdx-record->startIdx;
        if ( newScore >= set->champion ) {
            record->ownershipFlag=1;
            record->next=set->head;
            set->head=record;
            set->champion=newScore;
        }
        return GRR_RET_OK;
    }

    if ( depth == nfa->length ) {
        if ( character == GRR_NFA_TAB ) {
            fprintf(stderr,"Something went very wrong with the construction of the NFA!  An empty-transition loop has been found at state %zu while processing a '\\t'.\n", state);
        }
        else {
            fprintf(stderr,"Something went very wrong with the construction of the NFA!  An empty-transition loop has been found at state %zu while processing a '%c'.\n", state, character+GRR_NFA_ASCII_ADJUSTMENT);
        }
        abort();
    }

    nodes=nfa->nodes;
    for (unsigned int k=0; k<=nodes[state].twoTransitions; k++) {
        size_t newState;

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

            newRecord->ownershipFlag=1;
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
