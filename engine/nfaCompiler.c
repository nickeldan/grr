#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include "nfaCompiler.h"
#include "nfaInternals.h"
#include "grrUtil.h"

#define GRR_INVALID_CHARACTER 0x00
#define GRR_WHITESPACE_CODE 0x01
#define GRR_WILDCARD_CODE 0x02
#define GRR_EMPTY_TRANSITION_CODE 0x03
#define GRR_FIRST_CHAR_CODE 0x04
#define GRR_LAST_CHAR_CODE 0x05

#define GRR_NFA_PADDING 5

typedef struct nfaStackFrame {
	grrNfa nfa;
	size_t idx;
	char reason;
} nfaStackFrame;

typedef struct nfaStack {
	nfaStackFrame *frames;
	size_t length;
	size_t capacity;
} nfaStack;

#define NEW_NFA() calloc(1,sizeof(struct grrNfaStruct))

static void printIdxForString(const char *string, size_t len, size_t idx);
static int pushNfaToStack(nfaStack *stack, grrNfa nfa, size_t idx, char reason);
static void freeNfaStack(nfaStack *stack);
static ssize_t findParensInStack(nfaStack *stack);
static char resolveEscapeCharacter(char c) __attribute__ ((pure));
static grrNfa createCharacterNfa(char c);
static void setSymbol(nfaTransition *transition, char c);
static int concatenateNfas(grrNfa nfa1, grrNfa nfa2);
static int addDisjunctionToNfa(grrNfa nfa1, grrNfa nfa2);
static int checkForQuantifier(grrNfa nfa, char quantifier);

int grrCompilePattern(const char *string, size_t len, grrNfa *nfa) {
	int ret;
	nfaStack stack={0};
	grrNfa current;

	current=NEW_NFA();
	if ( !current ) {
		return GRR_RET_OUT_OF_MEMORY;
	}

	for (size_t idx=0; idx<len; idx++) {
		if ( !isprint(string[idx]) ) {
			fprintf(stderr,"Unprintable character at index %zu: 0x%02x\n", idx, (unsigned char)string[idx]);
			return GRR_RET_BAD_DATA;
		}
	}

	for (size_t idx=0; idx<len; idx++) {
		ssize_t stackIdx;
		char character;
		grrNfa temp;

		character=string[idx];
		switch ( character ) {
			case '(':
			case '|':
			ret=pushNfaToStack(&stack,current,idx,character)	;
			if ( ret != GRR_RET_OK ) {
				goto error;
			}
			current=NEW_NFA();
			if ( !current ) {
				ret=GRR_RET_OUT_OF_MEMORY;
				goto error;
			}
			break;

			case ')':
			stackIdx=findParensInStack(&stack);
			if ( stackIdx < 0 ) {
				fprintf(stderr,"Closing parenthesis not matched by preceding opening parenthesis:\n");
				printIdxForString(string,len,idx);
				ret=GRR_RET_BAD_DATA;
				goto error;
			}

			if ( stackIdx == stack.length-1 ) { // The parentheses were empty.
				grrFreeNfa(current);
			}
			else {
				temp=stack.frames[stackIdx+1].nfa;
				stack.frames[stackIdx+1].nfa=NULL;

				for (size_t k=stackIdx+2; k<stack.length; k++) {
					ret=addDisjunctionToNfa(temp,stack.frames[k].nfa);
					if ( ret != GRR_RET_OK ) {
						grrFreeNfa(temp);
						goto error;
					}
					stack.frames[k].nfa=NULL;
				}

				ret=checkForQuantifier(temp,string[idx+1]);
				if ( ret == GRR_RET_OK ) {
					idx++;
				}
				else if ( ret != GRR_RET_NOT_FOUND ) {
					grrFreeNfa(temp);
					goto error;
				}

				ret=addDisjunctionToNfa(temp,current);
				if ( ret != GRR_RET_OK ) {
					grrFreeNfa(temp);
					goto error;
				}
				current=temp;
				ret=concatenateNfas(stack.frames[stackIdx].nfa,current);
				if ( ret != GRR_RET_OK ) {
					goto error;
				}
			}
			current=stack.frames[stackIdx].nfa;
			stack.length=stackIdx;
			break;

			case '[':
			// 
			break;

			case '*':
			case '+':
			case '?':
			fprintf(stderr,"Invalid use of quantifier:\n");
			printIdxForString(string,len,idx);
			ret=GRR_RET_BAD_DATA;
			goto error;

			case '{':
			case '}':
			fprintf(stderr,"Curly braces must be escaped:\n");
			printIdxForString(string,len,idx);
			ret=GRR_RET_BAD_DATA;
			goto error;

			case '\\':
			character=resolveEscapeCharacter(string[++idx]);
			if ( character == GRR_INVALID_CHARACTER ) {
				fprintf(stderr,"Invalid character escape:\n");
				printIdxForString(string,len,idx);
				ret=GRR_RET_BAD_DATA;
				goto error;
			}
			goto add_character;

			case '.':
			character=GRR_WILDCARD_CODE;
			goto add_character;

			case '^':
			if ( current->length > 0 ) {
				fprintf(stderr,"'^' impossible to match:\n");
				printIdxForString(string,len,idx);
				ret=GRR_RET_BAD_DATA;
				goto error;
			}
			character=GRR_FIRST_CHAR_CODE;
			goto add_character;

			case '$':
			character=GRR_LAST_CHAR_CODE;
			goto add_character;

			default:
			add_character:
			temp=createCharacterNfa(character);
			if ( !temp ) {
				ret=GRR_RET_OUT_OF_MEMORY;
				goto error;
			}
			ret=checkForQuantifier(temp,string[idx+1]);
			if ( ret == GRR_RET_OK ) {
				idx++;
			}
			else if ( ret != GRR_RET_NOT_FOUND ) {
				grrFreeNfa(temp);
				goto error;
			}

			ret=concatenateNfas(current,temp);
			if ( ret != GRR_RET_OK ) {
				grrFreeNfa(temp);
				goto error;
			}
			break;
		}
	}

	for (size_t k=0; k<stack.length; k++) {
		if ( !stack.frames[k].nfa ) {
			continue;
		}

		if ( stack.frames[k].reason == '(' ) {
			fprintf(stderr,"Unclosed open parenthesis:\n");
			printIdxForString(string,len,stack.frames[k].idx);
			ret=GRR_RET_BAD_DATA;
			goto error;
		}
		ret=addDisjunctionToNfa(current,stack.frames[k].nfa);
		if ( ret != GRR_RET_OK ) {
			goto error;
		}
		stack.frames[k].nfa=NULL;
	}

	*nfa=current;
	return GRR_RET_OK;

	error:

	grrFreeNfa(current);
	freeNfaStack(&stack);

	return ret;
}

static void printIdxForString(const char *string, size_t len, size_t idx) {
	char tab='\t';

	write(STDERR_FILENO,&tab,1);
	write(STDERR_FILENO,string,len);
	fprintf(stderr,"\n\t");
	for (size_t k=0; k<idx; k++) {
		fprintf(stderr," ");
	}
	fprintf(stderr,"^\n");
}

static int pushNfaToStack(nfaStack *stack, grrNfa nfa, size_t idx, char reason) {
	if ( stack->length == stack->capacity ) {
		nfaStackFrame *success;
		size_t newCapacity;

		newCapacity=stack->capacity+GRR_NFA_PADDING;
		success=realloc(stack->frames,sizeof(*success)*newCapacity);
		if ( !success ) {
			return GRR_RET_OUT_OF_MEMORY;
		}

		stack->frames=success;
		stack->capacity=newCapacity;
	}

	stack->frames[stack->length].nfa=nfa;
	stack->frames[stack->length].idx=idx;
	stack->frames[stack->length].reason=reason;
	stack->length++;

	return GRR_RET_OK;
}

static void freeNfaStack(nfaStack *stack) {
	size_t len;

	len=stack->length;
	for (size_t k=0; k<len; k++) {
		grrFreeNfa(stack->frames[k].nfa);
		stack->frames[k].nfa=NULL;
	}
}

static ssize_t findParensInStack(nfaStack *stack) {
	for (ssize_t idx=(ssize_t)stack->length-1; idx>=0; idx--) {
		if ( stack->frames[idx].nfa && stack->frames[idx].reason == '(' ) {
			return idx;
		}
	}

	return -1;
}

static char resolveEscapeCharacter(char c) {
	switch ( c ) {
		case 't':
		return '\t';
		break;

		case '\\':
		case '(':
		case ')':
		case '[':
		case ']':
		case '{':
		case '}':
		case '.':
		case '*':
		case '+':
		case '?':
		case '^':
		case '$':
		case '|':
		return c;
		break;

		case 's':
		return GRR_WHITESPACE_CODE;

		default:
		return GRR_INVALID_CHARACTER;
	}
}

static grrNfa createCharacterNfa(char c) {
	grrNfa nfa;
	nfaNode *nodes;
	nfaTransition *transition;

	nodes=calloc(1,sizeof(*nodes));
	if ( !nodes ) {
		return NULL;
	}

	nodes->transitions=calloc(1,sizeof(*(nodes->transitions)));
	if ( !nodes->transitions ) {
		free(nodes);
		return NULL;
	}
	nodes->numTransitions=1;

	transition=nodes->transitions;
	transition->motion=1;
    setSymbol(transition,c);

	nfa=NEW_NFA();
	if ( !nfa ) {
		free(nodes->transitions);
		free(nodes);
		return NULL;
	}

	nfa->nodes=nodes;
	nfa->length=1;

	return nfa;
}

static void setSymbol(nfaTransition *transition, char c) {
    char c2;

    switch ( c ) {
        case GRR_WHITESPACE_CODE:
        transition->symbols[0] |= GRR_NFA_TAB_FLAG;
        c2=' '-GRR_NFA_ASCII_ADJUSTMENT;
        goto set_character;

        case GRR_WILDCARD_CODE:
        memset(transition->symbols+1,0xff,sizeof(transition->symbols)-1);
        transition->symbols[0] |= 0xfe;
        break;

        case GRR_EMPTY_TRANSITION_CODE:
        transition->symbols[0] |= GRR_NFA_EMPTY_CHARACTER_FLAG;
        break;

        case GRR_FIRST_CHAR_CODE:
        transition->symbols[0] |= GRR_NFA_FIRST_CHAR_FLAG;
        break;

        case GRR_LAST_CHAR_CODE:
        transition->symbols[0] |= GRR_NFA_LAST_CHAR_FLAG;
        break;

        case '\t':
        transition->symbols[0] |= GRR_NFA_TAB_FLAG;
        break;

        default:
        c2=c-GRR_NFA_ASCII_ADJUSTMENT;
        set_character:
        transition->symbols[c2/8] |= ( 1 << (c2%8) );
        break;
    }
}

static int concatenateNfas(grrNfa nfa1, grrNfa nfa2) {
    size_t newLen;
    nfaNode *success;

    newLen=nfa1->length+nfa2->length;
    success=realloc(nfa1->nodes,sizeof(*(nfa1->nodes))*newLen);
    if ( !success ) {
        return GRR_RET_OUT_OF_MEMORY;
    }

    nfa1->nodes=success;
    nfa1->length=newLen;

    memcpy(nfa1->nodes+nfa1->length,nfa2->nodes,sizeof(*(nfa2->nodes))*nfa2->length);
    grrFreeNfa(nfa2);

    return GRR_RET_OK;
}

static int addDisjunctionToNfa(grrNfa nfa1, grrNfa nfa2) {
    unsigned int newLen, len1, len2;
    nfaNode *success;

    len1=nfa1->length;
    len2=nfa2->length;
    newLen=1+len1+len2;
    success=realloc(nfa1->nodes,sizeof(*success)*newLen);
    if ( !success ) {
        return GRR_RET_OUT_OF_MEMORY;
    }

	memmove(nfa1->nodes+1,nfa1->nodes,sizeof(*(nfa1->nodes))*len1);
    memcpy(nfa1->nodes+1+len1,nfa2->nodes,sizeof(*(nfa2->nodes))*len2);
    nfa1->length=newLen;

    nfa1->nodes[0].transitions=calloc(2,sizeof(nfaTransition));
    if ( !nfa1->nodes[0].transitions ) {
        return GRR_RET_OUT_OF_MEMORY;
    }
    nfa1->nodes[0].numTransitions=2;

    setSymbol(nfa1->nodes[0].transitions,GRR_EMPTY_TRANSITION_CODE);
    nfa1->nodes[0].transitions[0].motion=1;

    setSymbol(nfa1->nodes[0].transitions+1,GRR_EMPTY_TRANSITION_CODE);
    nfa1->nodes[0].transitions[1].motion=len1+1;

    for (unsigned int k=0; k<len1; k++) {
        for (unsigned int j=0; j<nfa1->nodes[k+1].numTransitions; j++) {
            if ( (int)k + nfa1->nodes[k+1].transitions[j].motion == len1 ) {
                nfa1->nodes[k+1].transitions[j].motion += len2;
            }
        }
    }

    grrFreeNfa(nfa2);
    return GRR_RET_OK;
}

static int checkForQuantifier(grrNfa nfa, char quantifier) {
	unsigned int question=0, plus=0;

	switch ( quantifier ) {
		case '?':
		question=1;
		break;

		case '+':
		plus=1;
		break;

		case '*':
		question=plus=1;
		break;

		default:
		return GRR_RET_NOT_FOUND;
	}

	if ( question ) {
		unsigned int numTransitions;
		nfaTransition *success;

		numTransitions=nfa->nodes[0].numTransitions;
		success=realloc(nfa->nodes[0].transitions,sizeof(*success)*(numTransitions+1));
		if ( !success ) {
			return GRR_RET_OUT_OF_MEMORY;
		}

		memset(success[numTransitions].symbols,0,sizeof(success[numTransitions].symbols));
		setSymbol(success+numTransitions,GRR_EMPTY_TRANSITION_CODE);
		success[numTransitions].motion=nfa->length;
		nfa->nodes[0].transitions=success;
		nfa->nodes[0].numTransitions++;
	}

	if ( plus ) {
		int length;
		nfaNode *success;

		length=nfa->length;
		success=realloc(nfa->nodes,sizeof(*success)*(length+1));
		if ( !success ) {
			return GRR_RET_OUT_OF_MEMORY;
		}
		nfa->nodes=success;

		nfa->nodes[length].transitions=calloc(1,sizeof(nfaTransition));
		if ( !nfa->nodes[length].transitions ) {
			return GRR_RET_OUT_OF_MEMORY;
		}
		nfa->nodes[length].numTransitions=1;
		setSymbol(nfa->nodes[length].transitions,GRR_EMPTY_TRANSITION_CODE);
		nfa->nodes[length].transitions[0].motion=-1*length;

		nfa->length++;
	}

	return GRR_RET_OK;
}
