#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "nfa.h"

#define GRR_PATH_MAX 4096

typedef struct grrOptions {
    grrNfa filePattern;
    const char *editor;
    long lineNo;
    bool namesOnly;
    bool followSymbolicLinks;
    bool verbose;
} grrOptions;

void usage(const char *executable);
void searchDirectoryTree(DIR *dir, char *path, long *lineNo, const grrNfa nfa, const grrOptions *options);

int main(int argc, char **argv) {
    int ret=GRR_RET_OK, optval;
    long lineNo;
    grrNfa pattern=NULL;
    grrOptions options={0};
    char path[GRR_PATH_MAX]="./";
    char *temp;
    struct stat fileStat;
    DIR *dir;

    if ( argc == 1 ) {
        usage(argv[0]);
        return GRR_RET_BAD_DATA;
    }

    options.lineNo=-1;

    while ( (optval=getopt(argc,argv,":d:f:e:l:nsvh")) != -1 ) {
        switch ( optval ) {
            case 'd':
            temp=argv[optind-1];
            if ( !temp[0] ) {
                fprintf(stderr,"Starting directory cannot be empty.\n");
                ret=GRR_RET_BAD_DATA;
                goto done;
            }
            if ( temp[strlen(temp)-1] == '/' ) {
                ret=snprintf(path,sizeof(path),"%s", temp);
            }
            else {
                ret=snprintf(path,sizeof(path),"%s/", temp);
            }
            if ( ret >= sizeof(path) ) {
                fprintf(stderr,"Starting directory is too long (max. of %zu characters).\n", sizeof(path)-1);
                ret=GRR_RET_BAD_DATA;
                goto done;
            }
            ret=GRR_RET_OK;
            if ( stat(path,&fileStat) != 0 ) {
                perror("Could not stat starting directory");
                ret=GRR_RET_FILE_ACCESS;
                goto done;
            }

            if ( !S_ISDIR(fileStat.st_mode) ) {
                fprintf(stderr,"%s is not a directory.\n", path);
                ret=GRR_RET_BAD_DATA;
                goto done;
            }

            if ( access(path,X_OK) != 0 ) {
                perror("Could not access starting directory");
                ret=GRR_RET_FILE_ACCESS;
                goto done;
            }
            break;

            case 'f':
            ret=grrCompilePattern(optarg,strlen(optarg),&options.filePattern);
            if ( ret != GRR_RET_OK ) {
                fprintf(stderr,"Could not compile file pattern.\n");
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

            case 's':
            options.followSymbolicLinks=true;
            break;

            case 'v':
            options.verbose=true;
            break;

            case 'h':
            usage(argv[0]);
            goto done;

            case '?':
            fprintf(stderr,"Invalid option: %c\n", optopt);
            usage(argv[0]);
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

    if ( !options.editor ) {
        options.editor=getenv("DEFAULT_EDITOR");
        if ( !options.editor ) {
            option.editor=( access("vim",X_OK) == 0 )? "vim" : "vi";
        }
    }
    if ( access(options.editor,X_OK) != 0 ) {
        fprintf(stderr,"Cannot execute %s: %s", options.editor, strerror(errno));
        ret=GRR_RET_FILE_ACCESS;
        goto done;
    }

    ret=grrCompilePattern(argv[optind],strlen(argv[optind]),&pattern);
    if ( ret != GRR_RET_OK ) {
        fprintf(stderr,"Could not compile pattern.\n");
        goto done;
    }

    dir=opendir(path);
    if ( !dir ) {
        fprintf(stderr,"Could not access starting directory.\n");
        ret=GRR_RET_FILE_ACCESS;
        goto done;
    }
    lineNo=0;
    searchDirectoryTree(dir,path,&lineNo,pattern,&options);
    closedir(dir);

    done:

    if ( pattern ) {
        grrFreeNfa(pattern);
    }
    if ( options.filePattern ) {
        grrFreeNfa(options.filePattern);
    }

    return ret;
}

void usage(const char *executable) {
    printf("Usage: %s [options] pattern\n", executable);
    printf("Options:\n");
    printf("\t-f <file-pattern>   -- Only examine files whose names (excluding the directory) match this regex\n");
    printf("\t-e <editor>         -- When used in conjunction with -l, specifies the editor for opening files.\n");
    printf("\t                       Defaults to the DEFAULT_EDITOR environment variable or vi/vim if that is\n");
    printf("\t                       unset.\n");
    printf("\t-l <result-number>  -- Opens up the file specified in the l^th result.\n");
    printf("\t-n                  -- Display only the file names and not the individual lines within them.\n");
    printf("\t-s                  -- Follow symbolic links.\n");
    printf("\t-v                  -- Show verbose output.\n");
    printf("\t-h                  -- Display this message.\n");
}

void searchDirectoryTree(DIR *dir, char *path, long *lineNo, const grrNfa nfa, const grrOptions *options) {
    int ret;
    size_t offset;
    struct dirent *entry;

    offset=strlen(path);

    while ( (entry=readdir(dir)) ) {
        struct stat fileStat;
        
        path[offset]='\0';

        if ( snprintf(path,GRR_PATH_MAX,"%s%s", path, entry->d_name) >= GRR_PATH_MAX ) {
            if ( options->verbose ) {
                path[offset]='\0';
                fprintf(stderr,"Skipping file in %s directory because it its name is too long.\n", path);
            }
            continue;
        }

        if ( stat(path,&fileStat) != 0 ) {
            if ( options->verbose ) {
                fprintf(stderr,"Could not stat %s: %s\n", path, strerror(errno));
            }
            continue;
        }

        if ( S_ISREG(fileStat.st_mode) ) {

        }
        else if ( S_ISLNK(fileStat.st_mode) ) {
            if ( !options.followSymbolicLinks ) {
                continue;
            }


        }
        else if ( S_ISDIR(filestat.st_mode) ) {
            DIR *subdir;

            subdir=opendir(path);
            if ( !subdir ) {
                if ( options->verbose ) {
                    fprintf(stderr,"Could not access directory: %s\n", path);
                }
                continue;
            }

            searchDirectoryTree(subdir,path,lineNo,nfa,options);
            closedir(subdir);
        }
    }

    done:

    path[offset]='\0';

    return ret;
}
