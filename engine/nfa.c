#include <stdlib.h>

#include "nfaInternals.h"

void grrFreeNfa(grrNfa nfa) {
	if ( !nfa ) {
		return;
	}

    free(nfa->currentState.flags);
    free(nfa->nextState.flags);
	free(nfa->nodes);
	free(nfa);
}
