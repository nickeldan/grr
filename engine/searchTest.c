#include <stdio.h>
#include <string.h>

#include "nfa.h"

int main(int argc, char **argv) {
    int ret;
    size_t start, end;
    grrNfa nfa;

    if ( argc < 3 ) {
        fprintf(stderr,"Missing arguments\n");
        fprintf(stderr,"Usage: %s <regex> <string>\n", argv[0]);
        return -1;
    }

    ret=grrCompilePattern(argv[1],strlen(argv[1]),&nfa);
    if ( ret != GRR_RET_OK ) {
        printf("Failed to compile pattern.\n");
        return ret;
    }

    ret=grrSearch(nfa,argv[2],strlen(argv[2]),&start,&end,NULL,false);
    grrFreeNfa(nfa);
    if ( ret == GRR_RET_OK ) {
        printf("A string matched the regex from indices %zu to %zu.\n", start, end);
    }
    else if ( ret == GRR_RET_NOT_FOUND ) {
        printf("No substring match the regex.\n");
    }
    else {
        printf("Error while running grrSearch.\n");
    }

    return ret;
}
