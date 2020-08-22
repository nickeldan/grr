#ifndef __GRR_ENGINE_H__
#define __GRR_ENGINE_H__

#include <sys/types.h>

typedef struct grrNfaStruct *grrNfa;

int grrCompilePattern(const char *string, size_t len, grrNfa *nfa);

void grrFreeNfa(grrNfa nfa);

#endif // __GRR_ENGINE_H__