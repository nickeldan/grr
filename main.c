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
#include <sys/wait.h>

#include "engine/nfa.h"

#define GRR_PATH_MAX 4096

typedef struct grrOptions {
    grrNfa filePattern;
    const char *editor;
    long lineNo;
    bool namesOnly;
    bool verbose;
} grrOptions;

void usage(const char *executable);
int isExecutable(const char *path);
int searchDirectoryTree(DIR *dir, char *path, long *lineNo, const grrNfa nfa, const grrOptions *options);
int searchFileForPattern(const char *path, long *lineNo, const grrNfa nfa, const grrOptions *options);
void executeEditor(const char *editor, const char *path, long lineNo);

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

    ret=grrCompilePattern(argv[1],strlen(argv[1]),&pattern);
    if ( ret != GRR_RET_OK ) {
        fprintf(stderr,"Could not compile pattern.\n");
        return ret;
    }

    options.lineNo=-1;

    while ( (optval=getopt(argc-1,argv+1,":d:f:e:l:nvh")) != -1 ) {
        switch ( optval ) {
            case 'd':
            temp=argv[optind];
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

    if ( options.lineNo >= 0 ) {
        if ( !options.editor ) {
            options.editor=getenv("DEFAULT_EDITOR");
            if ( !options.editor ) {
                options.editor=( isExecutable("vim") == GRR_RET_OK )? "vim" : "vi";
            }
        }

        ret=isExecutable(options.editor);
        if ( ret != GRR_RET_OK ) {
            fprintf(stderr,"Cannot execute %s.\n", options.editor);
            goto done;
        }
    }
    else {
        options.editor=NULL;
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
    printf("Usage: %s pattern [options]\n", executable);
    printf("Options:\n");
    printf("\t-f <file-pattern>   -- Only examine files whose names (excluding the directory) match this regex\n");
    printf("\t-e <editor>         -- When used in conjunction with -l, specifies the editor for opening files.\n");
    printf("\t                       Defaults to the DEFAULT_EDITOR environment variable or vi/vim if that is\n");
    printf("\t                       unset.\n");
    printf("\t-l <result-number>  -- Opens up the file specified in the l^th result.\n");
    printf("\t-n                  -- Display only the file names and not the individual lines within them.\n");
    printf("\t-v                  -- Show verbose output.\n");
    printf("\t-h                  -- Display this message.\n");
}

int isExecutable(const char *path) {
    char line[GRR_PATH_MAX];
    FILE *f;

    if ( snprintf(line,sizeof(line),"which %s", path) >= sizeof(line) ) {
        return GRR_RET_OVERFLOW;
    }

    f=popen(line,"r");
    if ( !f ) {
        return GRR_RET_OUT_OF_MEMORY;
    }

    fgets(line,sizeof(line),f);
    pclose(f);

    return line[0]? GRR_RET_OK : GRR_RET_NOT_FOUND;
}

int searchDirectoryTree(DIR *dir, char *path, long *lineNo, const grrNfa nfa, const grrOptions *options) {
    int ret=GRR_RET_OK;
    size_t offset, newLen;
    struct dirent *entry;

    offset=strlen(path);

    while ( (entry=readdir(dir)) ) {
        struct stat fileStat;
        
        if ( strncmp(entry->d_name,".",2) == 0 || strncmp(entry->d_name,"..",3) == 0 ) {
            continue;
        }

        path[offset]='\0';

        strncat(path,entry->d_name,GRR_PATH_MAX);
        newLen=strlen(path);

        if ( stat(path,&fileStat) != 0 ) {
            if ( options->verbose ) {
                fprintf(stderr,"Could not stat %s: %s\n", path, strerror(errno));
            }
            continue;
        }

        if ( S_ISREG(fileStat.st_mode) ) {
            if ( options->filePattern && grrMatch(options->filePattern,entry->d_name,strlen(entry->d_name)) != GRR_RET_OK ) {
                continue;
            }

            if ( searchFileForPattern(path,lineNo,nfa,options) == GRR_RET_BREAK_LOOP ) {
                ret=GRR_RET_BREAK_LOOP;
                goto done;
            }
        }
        else if ( S_ISDIR(fileStat.st_mode) ) {
            DIR *subdir;

            if ( newLen+1 == GRR_PATH_MAX ) {
                if ( options->verbose ) {
                    path[offset]='\0';
                    fprintf(stderr,"Skipping subdirectory of %s because its name is too long.\n", path);
                }
                continue;
            }

            path[newLen++]='/';
            path[newLen]='\0';

            subdir=opendir(path);
            if ( !subdir ) {
                if ( options->verbose ) {
                    fprintf(stderr,"Could not access directory: %s\n", path);
                }
                continue;
            }

            ret=searchDirectoryTree(subdir,path,lineNo,nfa,options);
            closedir(subdir);
            if ( ret == GRR_RET_BREAK_LOOP ) {
                goto done;
            }
            ret=GRR_RET_OK;
        }
    }

    done:

    path[offset]='\0';

    return ret;
}

int searchFileForPattern(const char *path, long *lineNo, const grrNfa nfa, const grrOptions *options) {
    int ret=GRR_RET_NOT_FOUND;
    FILE *f;
    char line[2048];

    if ( options->verbose ) {
        fprintf(stderr,"Opening %s.\n", path);
    }

    f=fopen(path,"rb");
    if ( !f ) {
        if ( options->verbose ) {
            fprintf(stderr,"Could not read %s.\n", path);
        }
        return GRR_RET_FILE_ACCESS;
    }

    for (size_t fileLineNo=1; fgets(line,sizeof(line),f); fileLineNo++) {
        size_t len, start, end, offset;
        const char changeColorToRed[]={0x1b,'[','9','1','m'};
        const char restoreColor[]={0x1b,'[','m'};

        len=strlen(line);
        if ( len > 0 && line[len-1] == '\n' ) {
            len--;
        }
        for(; len>0 && line[len-1]=='\r'; len--);
        if ( len == 0 ) {
            continue;
        }
        ret=grrSearch(nfa,line,len,&start,&end,NULL,false);
        if ( ret == GRR_RET_BAD_DATA ) {
            if ( options->verbose ) {
                fprintf(stderr,"Terminating processing of %s since it contains non-printable data.\n", path);
            }
            break;
        }

        if ( ret != GRR_RET_OK ) {
            continue;
        }

        (*lineNo)++;

        if ( options->editor ) {
            if ( (*lineNo)-1 == options->lineNo ) {
                executeEditor(options->editor,path,fileLineNo);
                ret=GRR_RET_BREAK_LOOP;
                break;
            }
            else if ( options->namesOnly ) {
                break;
            }
            continue;
        }

        if ( options->namesOnly ) {
            printf("(%li) %s\n", (*lineNo)-1, path);
            break;
        }

        printf("(%li) %s (line %zu): ", (*lineNo)-1, path, fileLineNo);
        if ( start > 15 ) {
            printf("... ");
            offset=start-15;
        }
        else {
            offset=0;
        }
        fflush(stdout);

        write(STDOUT_FILENO,line+offset,start-offset);
        write(STDOUT_FILENO,changeColorToRed,sizeof(changeColorToRed));
        if ( end-start > 50 ) {
            write(STDOUT_FILENO,line+start,10);
            write(STDOUT_FILENO," ... ",5);
            write(STDOUT_FILENO,line+end-10,10);
        }
        else {
            write(STDOUT_FILENO,line+start,end-start);
        }
        write(STDOUT_FILENO,restoreColor,sizeof(restoreColor));

        if ( len-end > 15 ) {
            write(STDOUT_FILENO,line+end,15);
            write(STDOUT_FILENO," ...",4);
        }
        else {
            write(STDOUT_FILENO,line+end,len-end);
        }
        printf("\n");
    }

    fclose(f);

    return ret;
}

void executeEditor(const char *editor, const char *path, long lineNo) {
    pid_t child;
    int status;

    child=fork();
    switch ( child ) {
        case -1:
        perror("fork");
        return;

        case 0:
        if ( strncmp(editor,"vi",3) == 0 || strncmp(editor,"vim",4) == 0 ) {
            char argument[50];

            if ( snprintf(argument,sizeof(argument),"+%li", lineNo) >= sizeof(lineNo) ) {
                fprintf(stderr,"lineNo is too big to fit into buffer: %li\n", lineNo);
                exit(1);
            }
            execlp(editor,editor,argument,path,NULL);
        }
        else {
            execlp(editor,editor,path,NULL);
        }

        perror("execl");
        exit(1);

        default:
        while ( waitpid(child,&status,0) <= 0 );
        break;
    }
}
