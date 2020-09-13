#include <stdlib.h>

#include "nfaInternals.h"

void grrFreeNfa(grrNfa nfa) {
	if ( !nfa ) {
		return;
	}

    free(nfa->description);
	free(nfa->nodes);
	free(nfa);
}
