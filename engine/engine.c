#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include "engine.h"
#include "grrUtil.h"

typedef struct nfaTransition {
	unsigned char symbols[32];
	int motion;
} nfaTransition;

typedef struct nfaNode {
	nfaTransition *transitios;
	unsigned int numTransitions;
} nfaNode;

typedef struct nfaStackFrame {
	grrNfa nfa;
	size_t idx;
	char reason;
} nfaStackFrame;

typedef struct nfaStack {
	nfaStackFrame *frames;
	size_t capacity;
	size_t length;
} nfaStack;

struct grrNfaStruct {
	nfaNode *nodes;
	size_t length;
	size_t capacity;
};

#define NEW_NFA() calloc(1,sizeof(*grrNfa))

static void printIdxForString(const char *string, size_t len, size_t idx);
static int pushNfaToStack(nfaStack *stack, grrNfa nfa, size_t idx, char reason);
static void freeNfaStack(nfaStack *stack);
static ssize_t findParensInStack(nfaStack *stack);
static char resolveEscapeCharacter(char c) __attribute__ ((pure));
static grrNfa createCharacterNfa(char c);
static int concatenateNfas(grrNfa nfa1, grrNfa nfa2);
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
		if ( !isprint(string[k]) ) {
			fprintf(stderr,"Unprintable character at index %zu: 0x%02x\n", idx, (unsigned char)string[idx]);
			return GRR_RET_BAD_DATA;
		}
	}

	for (size_t idx=0; idx<len; idx++) {
		ssize_t stackIdx;
		char character;
		grrNfa temp;

		switch ( string[idx] ) {
			case '^':
			// 
			break;

			case '$':
			// 
			break;

			case '(':
			ret=pushNfaToStack(&stack,current,idx,'(');
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

			if ( stackIdx < stack.length-1 ) {
				temp=stack.frames[stackIdx+1].nfa;
				stack.frames[stackIdx+1].nfa=NULL;

				for (ssize_t k=stackIdx+2; k<stack.length; k++) {
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
			else { // The parentheses were empty.
				grrFreeNfa(current);
			}
			current=stack.frames[stackIdx].nfa;
			stack.frames.length=stackIdx;
			break;

			case '|':
			ret=pushNfaToStack(&stack,current,idx,'|');
			if ( ret != GRR_RET_OK ) {
				goto error;
			}
			current=NEW_NFA();
			if ( !current ) {
				ret=GRR_RET_OUT_OF_MEMORY;
				goto error;
			}
			break;

			case '\\':
			character=resolveEscapeCharacter(string[++idx]);
			if ( character == '\0' ) {
				fprintf(stderr,"Invalid character escape:\n");
				printIdxForString(string,len,idx);
				ret=GRR_RET_BAD_DATA;
				goto error;
			}

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

			case '[':
			// 
			break;

			case '{':
			case '}':
			fprintf(stderr,"Curly braces must be escaped:\n");
			printIdxForString(string,len,idx);
			ret=GRR_RET_BAD_DATA;
			goto error;

			case '\t':
			fprintf(stderr,"Illegal tab character found at position %zu.  An explicit '\\t' is required.\n", idx);
			ret=GRR_RET_BAD_DATA;
			goto error;

			case '\r':
			case '\n':
			fprintf(stderr,"Illegal newline character found at position %zu.\n", idx);
			ret=GRR_RET_BAD_DATA;
			goto error;

			default:
			temp=createCharacterNfa(string[idx]);
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

	for (size_t k=0; k<stack.len; k++) {
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

void grrFreeNfa(grrNfa nfa) {
	size_t len;

	if ( !nfa ) {
		return;
	}

	len=nfa->length;
	for (size_t k=0; k<len; k++) {
		free(nfa->nodes[k].transitions);
	}

	free(nfa);
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

static int pushNfaToStack(nfaStack *stack, grrNfa *nfa, size_t idx, char reason) {

}

static void freeNfaStack(nfaStack *stack) {

}

static ssize_t findParensInStack(nfaStack *stack) {
	
}

static char resolveEscapeCharacter(char c) {
	switch ( c ) {
		case 'n':
		return '\n';
		break;

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
		return c;
		break;

		case 's': // Whitespace
		return 0x01;

		default: // Invalid escape character.
		return '\0';
	}
}

static grrNfa createCharacterNfa(char c) {

}

static int concatenateNfas(grrNfa nfa1, grrNfa nfa2) {

}

static int checkForQuantifier(grrNfa nfa, char quantifier) {

}