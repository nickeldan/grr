#ifndef __GRR_RUNTIME_H__
#define __GRR_RUNTIME_H__

#include <sys/types.h>

#include "nfaDef.h"

int grrNfaMatch(const grrNfa nfa, const char *string, size_t len);

int grrNfaSearch(const grrNfa nfa, const char *string, size_t len, size_t *start, size_t *end);

#endif // __GRR_RUNTIME_H__