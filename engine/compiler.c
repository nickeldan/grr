#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include "compiler.h"
#include "nfaInternals.h"
#include "grrUtil.h"

#define GRR_INVALID_CHARACTER 0x00
#define GRR_WHITESPACE_CODE 0x01
#define GRR_WILDCARD_CODE 0x02
#define GRR_EMPTY_TRANSITION_CODE 0x03

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
#define SET_SYMBOL(transition,c) (transition)->symbols[(c)/8] |= ( 1 << ((c)%8) )

static void printIdxForString(const char *string, size_t len, size_t idx);
static int pushNfaToStack(nfaStack *stack, grrNfa nfa, size_t idx, char reason);
static void freeNfaStack(nfaStack *stack);
static ssize_t findParensInStack(nfaStack *stack);
static char resolveEscapeCharacter(char c) __attribute__ ((pure));
static grrNfa createCharacterNfa(char c);
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
			case '^':
			// 
			break;

			case '$':
			// 
			break;

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
			if ( stackIdx == -1 ) {
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

				for (ssize_t k=stackIdx+2; (size_t)k<stack.length; k++) {
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

	nodes=calloc(GRR_NFA_PADDING,sizeof(*nodes));
	if ( !nodes ) {
		return NULL;
	}

	nodes->transitions=calloc(1,sizeof(*(nodes->transitions)));
	if ( !nodes->transitions ) {
		free(nodes);
		return NULL;
	}
	nodes->length=nodes->capacity=1;

	transition=nodes->transitions;
	transition->motion=1;

	switch ( c ) {
		case GRR_WHITESPACE_CODE:
		SET_SYMBOL(transition,' ');
		SET_SYMBOL(transition,'\t');
		break;

		case GRR_WILDCARD_CODE:
		memset(transition->symbols,0xff,sizeof(transition->symbols));
		transition->symbols[0]=0xfe;
		break;

		case GRR_EMPTY_TRANSITION_CODE:
		transition->symbols[0]=0x01;
		break;

		default:
		SET_SYMBOL(transition,c);
		break;
	}

	nfa=NEW_NFA();
	if ( !nfa ) {
		free(nodes->transitions);
		free(nodes);
		return NULL;
	}

	nfa->nodes=nodes;
	nfa->length=1;
	nfa->capacity=GRR_NFA_PADDING;

	return nfa;
}

static int concatenateNfas(grrNfa nfa1, grrNfa nfa2) {
	return GRR_RET_NOT_IMPLEMENTED;
}

static int addDisjunctionToNfa(grrNfa nfa1, grrNfa nfa2) {
	return GRR_RET_NOT_IMPLEMENTED;
}

static int checkForQuantifier(grrNfa nfa, char quantifier) {
	return GRR_RET_NOT_IMPLEMENTED;
}