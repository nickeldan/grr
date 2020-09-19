#ifndef __GRR_COMPILER_H__
#define __GRR_COMPILER_H__

#include <sys/types.h>

#include "nfaDef.h"

int grrCompile(const char *string, size_t len, grrNfa *nfa);

#endif // __GRR_COMPILER_H__
