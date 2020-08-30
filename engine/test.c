#include <stdio.h>
#include <string.h>

#include "nfa.h"

int main(int argc, char **argv) {
    int ret;
    grrNfa nfa;

    if ( argc < 3 ) {
        fprintf(stderr,"Missing arguments\n");
        fprintf(stderr,"Usage: %s <regex> <string>\n", argv[0]);
        return -1;
    }

    ret=grrCompilePattern(argv[1],strlen(argv[1]),&nfa);
    if ( ret != GRR_RET_OK ) {
        printf("Failed to compile pattern\n");
        return ret;
    }

    ret=grrMatch(nfa,argv[2],strlen(argv[2]));
    grrFreeNfa(nfa);
    if ( ret == GRR_RET_OK ) {
        printf("The string matched the regex\n");
    }
    else if ( ret == GRR_RET_NOT_FOUND ) {
        printf("The string did not match the regex\n");
    }
    else {
        printf("Error while running grrMatch\n");
    }

    return ret;
}
