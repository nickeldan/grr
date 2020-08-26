#include <stdlib.h>

#include "nfaInternals.h"

void grrFreeNfa(grrNfa nfa) {
	size_t len;

	if ( !nfa ) {
		return;
	}

	len=nfa->length;
	for (size_t k=0; k<len; k++) {
		free(nfa->nodes[k].transitions);
	}

	free(nfa->nodes);
	free(nfa);
}