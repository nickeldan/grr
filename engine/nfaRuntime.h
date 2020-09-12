#ifndef __GRR_RUNTIME_H__
#define __GRR_RUNTIME_H__

#include <stdbool.h>
#include <sys/types.h>

#include "nfaDef.h"

int grrMatch(grrNfa nfa, const char *string, size_t len);

int grrSearch(grrNfa nfa, const char *string, size_t len, size_t *start, size_t *end, size_t *cursor, bool tolerateNonprintables);

#endif // __GRR_RUNTIME_H__
