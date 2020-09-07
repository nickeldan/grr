#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdbool.h>
#include <getopt.h>

#include "nfa.h"

typedef struct grrOptions {
    grrNfa filePattern;
    const char *editor;
    long lineNo;
    bool namesOnly;
} grrOptions;

void usage(void);

int main(int argc, char **argv) {
    int ret=GRR_RET_OK, optval;
    grrNfa pattern=NULL;
    grrOptions options={.filePattern=NULL, .editor=NULL, .lineNo=-1, .namesOnly=false};
    char *temp;

    while ( (optval=getopt(argc,argv,":f:e:l:nh")) != -1 ) {
        switch ( optval ) {
            case 'f':
            ret=grrCompilePattern(optarg,strlen(optarg),&options.filePattern);
            if ( ret != GRR_RET_OK ) {
                goto done;
            }
            break;

            case 'e':
            options.editor=optarg;
            break;

            case 'l':
            options.lineNo=strtol(optarg,&temp,10);
            if ( temp == optarg || *temp || options.lineNo < 0 || ( options.lineNo == LONG_MAX && errno == ERANGE ) ) {
                fprintf(stderr,"Invalid 'l' option: %s\n", optarg);
                ret=GRR_RET_BAD_DATA;
                goto done;
            }
            break;

            case 'n':
            options.namesOnly=true;
            break;

            case 'h':
            usage();
            goto done;

            case '?':
            fprintf(stderr,"Invalid option: %c\n", optopt);
            usage();
            ret=GRR_RET_BAD_DATA;
            goto done;

            case ':':
            fprintf(stderr,"-%c requires an argument.\n", optopt);
            ret=GRR_RET_BAD_DATA;
            goto done;

            default:
            abort();
        }
    }

    printf("%i\n", optind);

    done:

    if ( pattern ) {
        grrFreeNfa(pattern);
    }
    if ( options.filePattern ) {
        grrFreeNfa(options.filePattern);
    }

    return ret;
}

void usage(void) {

}
