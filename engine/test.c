#include <stdio.h>

#include "nfa.h"
#include "grrUtil.h"

int main() {
    int ret;
    size_t start, end;
    grrNfa nfa;
    const char pattern[]="ab+c";
    const char matchString[]="abbbbbbbbc";
    const char searchString[]="lkjasldjjkljsadlkfjabbbbbbek1999abbckkkk";

    printf("Compiling the regex: %s\n", pattern);

    ret=grrCompilePattern(pattern,sizeof(pattern)-1,&nfa);
    if ( ret != GRR_RET_OK ) {
        return ret;
    }

    printf("Regex compiled\n");

    ret=grrMatch(nfa,matchString,sizeof(matchString)-1);
    if ( ret == GRR_RET_OK ) {
        printf("%s matched the regex.\n", matchString);
    }
    else if ( ret == GRR_RET_NOT_FOUND ) {
        printf("%s did not match the regex.\n", matchString);
    }
    else {
        return ret;
    }

    ret=grrSearch(nfa,searchString,sizeof(searchString)-1,&start,&end);
    if ( ret == GRR_RET_OK ) {
        printf("A substring of %s matched the regex (from indices %zu to %zu).\n", searchString, start, end);
    }
    else if ( ret == GRR_RET_NOT_FOUND ) {
        printf("No substring of %s matched the regex.\n", searchString);
    }
    else {
        return ret;
    }

    return 0;
}
