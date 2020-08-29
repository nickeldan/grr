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
#define GRR_DIGIT_CODE 0x06

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
static ssize_t findParensInStack(const nfaStack *stack);
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

	for (size_t idx=0; idx<len; idx++) {
		if ( !isprint(string[idx]) ) {
			fprintf(stderr,"Unprintable character at index %zu: 0x%02x\n", idx, (unsigned char)string[idx]);
			return GRR_RET_BAD_DATA;
		}
	}

	current=NEW_NFA();
	if ( !current ) {
		return GRR_RET_OUT_OF_MEMORY;
	}

	for (size_t idx=0; idx<len; idx++) {
		size_t idx2;
		ssize_t stackIdx;
		char character;
		int negation;
		grrNfa temp;
		nfaTransition transition;

		character=string[idx];
		switch ( character ) {
			case '(':
			case '|':
			ret=pushNfaToStack(&stack,current,idx,character);
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

				ret=addDisjunctionToNfa(temp,current);
				if ( ret != GRR_RET_OK ) {
					grrFreeNfa(temp);
					goto error;
				}
				current=temp;

                if ( idx+1 < len ) {
                    ret=checkForQuantifier(current,string[idx+1]);
                    if ( ret == GRR_RET_OK ) {
                        idx++;
                    }
                    else if ( ret != GRR_RET_NOT_FOUND ) {
                        grrFreeNfa(temp);
                        goto error;
                    }
                }

				ret=concatenateNfas(stack.frames[stackIdx].nfa,current);
				if ( ret != GRR_RET_OK ) {
					goto error;
				}
			}

			current=stack.frames[stackIdx].nfa;
			stack.length=stackIdx;
			break;

			case '[':
			if ( idx == len-1 ) {
				fprintf(stderr,"Unclosed character class:\n");
				printIdxForString(string,len,idx);
				ret=GRR_RET_BAD_DATA;
				goto error;
			}
			memset(&transition,0,sizeof(transition));
			negation=( string[idx+1] == '^' );
			idx2=idx++;
			if ( idx < len && string[idx] == '-' ) {
				setSymbol(&transition,'-');
				idx++;
			}
			while ( idx < len-1 && string[idx] != ']' ) {
				character=string[idx];

				if ( string[idx+1] == '-' ) {
					char possibleRangeEnd, character2;

					if ( idx == len-2 ) {
						fprintf(stderr,"Unclosed range in character class:\n");
						printIdxForString(string,len,idx2);
						ret=GRR_RET_BAD_DATA;
						goto error;
					}

					if ( character >= 'A' && character < 'Z' ) {
						possibleRangeEnd='Z';
					}
					else if ( character >= 'a' && character < 'z' ) {
						possibleRangeEnd='z';
					}
					else if ( character >= '0' && character < '9' ) {
						possibleRangeEnd='9';
					}
					else {
						fprintf(stderr,"Invalid character class range:\n");
						printIdxForString(string,len,idx);
						ret=GRR_RET_BAD_DATA;
						goto error;
					}

					character2=string[idx+2];
					if ( ! ( character2 > character && character2 <= possibleRangeEnd ) ) {
						fprintf(stderr,"Invalid character class range:\n");
						printIdxForString(string,len,idx);
						ret=GRR_RET_BAD_DATA;
						goto error;
					}

					for (char c=character; c<=character2; c++) {
						setSymbol(&transition,c);
					}

					idx+=3;
					continue;
				}

				if ( character == '\\' ) {
					character=string[idx+1];

					switch ( character ) {
						case '[':
						case ']':
						break;

						case 't':
						character='\t';
						break;

						default:
						fprintf(stderr,"Invalid character escape:\n");
						printIdxForString(string,len,idx);
						ret=GRR_RET_BAD_DATA;
						goto error;
					}

					idx+=2;
				}
				else {
					idx++;
				}

				setSymbol(&transition,character);
			}

			if ( string[idx] != ']' ) {
				fprintf(stderr,"Unclosed character class:\n");
				printIdxForString(string,len,idx2);
				ret=GRR_RET_BAD_DATA;
				goto error;
			}

            if ( negation ) {
                for (size_t k=0; k<sizeof(transition.symbols); k++) {
                    transition.symbols[k] ^= 0xff;
                }
                transition.symbols[0] &= ~GRR_NFA_EMPTY_TRANSITION_FLAG;
            }

            temp=NEW_NFA();
            if ( !temp ) {
                ret=GRR_RET_OUT_OF_MEMORY;
                goto error;
            }
            temp->nodes=calloc(1,sizeof(*(temp->nodes)));
            if ( !temp->nodes ) {
                grrFreeNfa(temp);
                ret=GRR_RET_OUT_OF_MEMORY;
                goto error;
            }
            memcpy(&temp->nodes[0].transitions[0],&transition,sizeof(transition));

            if ( idx+1 < len ) {
                ret=checkForQuantifier(temp,string[idx+1]);
                if ( ret == GRR_RET_OK ) {
                    idx++;
                }
                else if ( ret != GRR_RET_NOT_FOUND ) {
                    grrFreeNfa(temp);
                    goto error;
                }
            }

            ret=concatenateNfas(current,temp);
            if ( ret != GRR_RET_OK ) {
                grrFreeNfa(temp);
                goto error;
            }
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

            if ( idx+1 < len ) {
                ret=checkForQuantifier(temp,string[idx+1]);
                if ( ret == GRR_RET_OK ) {
                    idx++;
                }
                else if ( ret != GRR_RET_NOT_FOUND ) {
                    grrFreeNfa(temp);
                    goto error;
                }
            }

			ret=concatenateNfas(current,temp);
			if ( ret != GRR_RET_OK ) {
				grrFreeNfa(temp);
				goto error;
			}
			break;
		}
	}

	for (ssize_t k=stack.length-1; k>=0; k--) {
		if ( !stack.frames[k].nfa ) {
			continue;
		}

		if ( stack.frames[k].reason == '(' ) {
			fprintf(stderr,"Unclosed open parenthesis:\n");
			printIdxForString(string,len,stack.frames[k].idx);
			ret=GRR_RET_BAD_DATA;
			goto error;
		}
		ret=addDisjunctionToNfa(stack.frames[k].nfa,current);
		if ( ret != GRR_RET_OK ) {
			goto error;
		}
		current=stack.frames[k].nfa;
		stack.frames[k].nfa=NULL;
	}
    free(stack.frames);

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

static ssize_t findParensInStack(const nfaStack *stack) {
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

		case 'd':
		return GRR_DIGIT_CODE;

		default:
		return GRR_INVALID_CHARACTER;
	}
}

static grrNfa createCharacterNfa(char c) {
	grrNfa nfa;
	nfaNode *nodes;

	nodes=calloc(1,sizeof(*nodes));
	if ( !nodes ) {
		return NULL;
	}

	nodes->transitions[0].motion=1;
	setSymbol(&nodes->transitions[0],c);
    if ( c == GRR_NFA_FIRST_CHAR_FLAG || c == GRR_NFA_LAST_CHAR_FLAG ) {
        setSymbol(&nodes->transitions[0],GRR_NFA_EMPTY_TRANSITION_FLAG);
    }

	nfa=NEW_NFA();
	if ( !nfa ) {
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
        transition->symbols[0] |= GRR_NFA_EMPTY_TRANSITION_FLAG;
        break;

        case GRR_FIRST_CHAR_CODE:
        transition->symbols[0] |= GRR_NFA_FIRST_CHAR_FLAG;
        break;

        case GRR_LAST_CHAR_CODE:
        transition->symbols[0] |= GRR_NFA_LAST_CHAR_FLAG;
        break;

        case GRR_DIGIT_CODE:
        for (int k=0; k<10; k++) {
        	c2='0'+k-GRR_NFA_ASCII_ADJUSTMENT;
        	transition->symbols[c2/8] |= ( 1 << (c2%8) );
        }
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

    memcpy(nfa1->nodes+nfa1->length,nfa2->nodes,sizeof(*(nfa2->nodes))*nfa2->length);
    grrFreeNfa(nfa2);
    nfa1->length=newLen;

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
    nfa1->nodes=success;

	memmove(success+1,success,sizeof(*success)*len1);
    memcpy(success+1+len1,nfa2->nodes,sizeof(*(nfa2->nodes))*len2);
    nfa1->length=newLen;

    memset(success,0,sizeof(*success));
    success[0].twoTransitions=1;
    for (int k=0; k<2; k++) {
    	setSymbol(&success[0].transitions[k],GRR_EMPTY_TRANSITION_CODE);
    }
    success[0].transitions[0].motion=1;
    success[0].transitions[1].motion=len1+1;

    for (unsigned int k=0; k<len1; k++) {
        for (unsigned int j=0; j<=success[k+1].twoTransitions; j++) {
            if ( (int)k + success[k+1].transitions[j].motion == len1 ) {
                success[k+1].transitions[j].motion += len2;
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
		if ( nfa->nodes[0].twoTransitions ) {
			nfaNode *success;

			success=realloc(nfa->nodes,sizeof(*success)*(nfa->length+1));
			if ( !success ) {
				return GRR_RET_OUT_OF_MEMORY;
			}
			nfa->nodes=success;

			memmove(success+1,success,sizeof(*success)*nfa->length);
			memset(success,0,sizeof(*success));
			success[0].twoTransitions=1;

			for (int k=0; k<2; k++) {
				setSymbol(&success[0].transitions[k],GRR_EMPTY_TRANSITION_CODE);
			}
			success[0].transitions[0].motion=1;
			success[0].transitions[1].motion=nfa->length+1;

			nfa->length++;
		}
		else {
			nfaNode *node;

			node=nfa->nodes;
			memset(node->transitions[1].symbols,0,sizeof(nfa->nodes[0].transitions[1].symbols));
			setSymbol(&node->transitions[1],GRR_EMPTY_TRANSITION_CODE);
			node->transitions[1].motion=nfa->length;
			node->twoTransitions=1;
		}
	}

	if ( plus ) {
		unsigned int length;
		nfaNode *success;

		length=nfa->length;
		success=realloc(nfa->nodes,sizeof(*success)*(length+1));
		if ( !success ) {
			return GRR_RET_OUT_OF_MEMORY;
		}
		nfa->nodes=success;

		memset(success+length,0,sizeof(*success));
        success[length].twoTransitions=1;
        for (int k=0; k<2; k++) {
            setSymbol(&success[length].transitions[k],GRR_EMPTY_TRANSITION_CODE);
        }
		success[length].transitions[0].motion=-1*(int)length;
        success[length].transitions[1].motion=1;

		nfa->length++;
	}

	return GRR_RET_OK;
}
