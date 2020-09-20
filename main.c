/*
Written by Daniel Walker, 2020.
*/


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
#define GRR_HISTORY "~/.grr_history"

typedef struct grrOptions {
    char *pattern_string;
    char *file_pattern_string;
    const char *starting_directory;
    const char *editor;
    grrNfa filePattern;
    long lineNo;
    int logger_fd;
    bool names_only;
    bool verbose;
    bool ignore_hidden;
    bool colorless;
} grrOptions;

typedef struct grrSimpleOptions {
    bool file_pattern;
    bool names_only;
    bool ignore_hidden;
} grrSimpleOptions;

void usage(const char *executable);
int isExecutable(const char *path);
int compareOptionsToHistory(const grrOptions *options);
bool readLine(FILE *f, char *destination, size_t size);
int searchDirectoryTree(DIR *dir, char *path, long *lineNo, const grrNfa nfa, const grrOptions *options);
int searchFileForPattern(const char *path, long *lineNo, const grrNfa nfa, const grrOptions *options);
int executeEditor(const char *editor, const char *path, long lineNo);

int main(int argc, char **argv) {
    int ret=GRR_RET_OK, optval;
    long lineNo;
    grrNfa pattern=NULL;
    grrOptions options={0};
    char path[GRR_PATH_MAX]="./";
    DIR *dir;

    if ( argc == 1 ) {
        usage(argv[0]);
        return GRR_RET_BAD_DATA;
    }

    ret=grrCompile(argv[1],strlen(argv[1]),&pattern);
    if ( ret != GRR_RET_OK ) {
        fprintf(stderr,"Could not compile pattern.\n");
        return ret;
    }

    options.lineNo=-1;

    while ( (optval=getopt(argc-1,argv+1,":d:f:e:l:nicv")) != -1 ) {
        struct stat file_stat;
        char *temp;

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
            if ( stat(path,&file_stat) != 0 ) {
                perror("Could not stat starting directory");
                ret=GRR_RET_FILE_ACCESS;
                goto done;
            }

            if ( !S_ISDIR(file_stat.st_mode) ) {
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
            ret=grrCompile(optarg,strlen(optarg),&options.filePattern);
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
            options.names_only=true;
            break;

            case 'i':
            options.ignore_hidden=true;
            break;

            case 'c':
            options.colorless=true;
            break;

            case 'v':
            options.verbose=true;
            break;

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

    options.starting_directory=path;

    if ( options.lineNo >= 0 ) {
        options.logger_fd=-1;

        if ( !options.editor ) {
            options.editor=getenv("EDITOR");
            if ( !options.editor ) {
                options.editor=( isExecutable("vim") == GRR_RET_OK )? "vim" : "vi";
            }
        }

        ret=isExecutable(options.editor);
        if ( ret != GRR_RET_OK ) {
            fprintf(stderr,"Cannot execute %s.\n", options.editor);
            goto done;
        }

        ret=compareOptionsToHistory(&options);
        if ( ret == GRR_RET_OK ) {
            goto done;
        }
    }
    else {
        char tmp_file[]="./.grrtempXXXXXX";

        options.editor=NULL;
        options.logger_fd=mkstemp(tmp_file);
        if ( options.logger_fd == -1 ) {
            perror("mkstemp");

            ret=GRR_RET_FILE_ACCESS;
            goto done;
        }
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
    if ( options.logger_fd >= 0 ) {
        close(options.logger_fd);
    }

    return ret;
}

void usage(const char *executable) {
    printf("Usage: %s pattern [options]\n", executable);
    printf("Options:\n");
    printf("\t-f <file-pattern>   -- Only examine files whose names (excluding the directory) match this regex\n");
    printf("\t-e <editor>         -- When used in conjunction with -l, specifies the editor for opening files.\n");
    printf("\t                       Defaults to the EDITOR environment variable or vi/vim if that is unset.\n");
    printf("\t-l <result-number>  -- Opens up the file specified in the l^th result.\n");
    printf("\t-n                  -- Display only the file names and not the individual lines within them.\n");
    printf("\t-i                  -- Ignore hidden files and directories.\n");
    printf("\t-v                  -- Show verbose output.\n");
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

int compareOptionsToHistory(const grrOptions *options) {
    int ret=GRR_RET_BAD_DATA, fd;
    size_t len;
    ssize_t value;
    char line[GRR_PATH_MAX+10], symbolic_link[50], starting_directory[GRR_PATH_MAX];
    FILE *f;
    grrSimpleOptions observed_options={0};

    f=fopen(GRR_HISTORY,"rb");
    if ( !f ) {
        if ( options->verbose ) {
            fprintf(stderr,"Failed to open %s: %s\n", GRR_HISTORY, strerror(errno));
        }

        return GRR_RET_FILE_ACCESS;
    }

    if ( readLine(f,line,sizeof(line)) ) {
        goto failed_read;
    }
    if ( strcmp(line,options->file_pattern) != 0 ) {
        goto done;
    }

    if ( readLine(f,line,sizeof(line)) ) {
        goto failed_read;
    }

    fd=open(options->starting_directory,O_RDONLY);
    if ( fd == -1 ) {
        if ( options->verbose ) {
            fprintf(stderr,"Failed to access %s: %s", options->starting_directory, strerror(errno));
        }

        ret=GRR_RET_FILE_ACCESS;
        goto done;
    }
    if ( snprintf(symbolic_link,sizeof(symbolic_link),"/proc/%i/fd/%i", getpid(), fd) >= sizeof(symbolic_link) ) {
        if ( options->verbose ) {
            fprintf(stderr,"The string '/proc/%i/fd/%i' overflowed the buffer.\n", getpid(), fd);
        }

        close(fd);
        ret=GRR_RET_OVERFLOW;
        goto done;
    }
    value=readlink(symbolic_link,starting_directory,sizeof(starting_directory));
    close(fd);
    if ( value == -1 ) {
        if ( options->verbose ) {
            int local_errno;

            local_errno=errno;
            fprintf(stderr,"Failed to read symbolic link at '/proc/%i/fd/%i': %s", getpid(), fd, local_errno);
        }

        ret=GRR_RET_FILE_ACCESS;
        goto done;
    }

    if ( strcmp(line,starting_directory) != 0 ) {
        goto done;
    }

    if ( readLine(f,line,sizeof(line)) ) {
        goto failed_read;
    }

    len=strlen(line);
    for (size_t k=0; k<len; k++) {
        switch ( line[k] ) {
            case 'f':
            observed_options.file_pattern=true;
            break;

            case 'n':
            observed_options.names_only=true;
            break;

            case 'i':
            observed_options.ignore_hidden=true;
            break;

            default:
            if ( options->verbose ) {
                fprintf(stderr,"Invalid data found on option line of %s.\n", GRR_HISTORY);
            }
            goto done;
        }
    }

    if ( observed_options.names_only != options->names_only ) {
        goto done;
    }

    if ( observed_options.ignore_hidden != options->ignore_hidden ) {
        goto done;
    }

    if ( observed_options.file_pattern ) {
        if ( !options->file_pattern ) {
            goto done;
        }

        if ( !readLine(f,line,sizeof(line)) ) {
            goto failed_read;
        }
        if ( strcmp(line,options->file_pattern_string) != 0 ) {
            goto done;
        }
    }
    else if ( options->file_pattern ) {
        goto done;
    }

    for (long k=0; k<options->line_no; k++) {
        if ( !readLine(f,line,sizeof(line)) ) {
            goto failed_read;
        }
    }

    if ( observed_options.names_only ) {
        ret=executeEditor(options->editor,line,1);
    }
    else {
        char *colon_ptr, *temp;
        long line_no;

        colon_ptr=strstr(line,":");
        if ( !colon_ptr || colon_ptr[1] == '\0' ) {
            if ( options->verbose ) {
                fprintf(stderr,"No ':' found for result '%s' in %s.\n", line, GRR_HISTORY);
            }

            goto done;
        }

        line[colon_ptr-(char*)line]='\0';
        line_no=strtol(colon_ptr+1,&temp,10);
        if ( temp[0] != '\0'
                || ( line_no == LONG_MAX && errno == ERANGE )
                || line_no < 1
        ) {
            if ( options->verbose ) {
                fprintf(stderr,"Invalid line number found for result '%s' in %s.\n", line, GRR_HISTORY);
            }

            goto done;
        }

        ret=executeEditor(options->editor,line,line_no);
    }
    ret=( ret == 0 )? GRR_RET_OK : GRR_RET_EXEC;
    goto done;

    failed_read:

    if ( options->verbose ) {
        if ( ferror(f) ) {
            fprintf(stderr,"Failed to read from %s.\n", GRR_HISTORY);
        }
        else {
            fprintf(stderr,"Unexpected end of file found in %s.\n", GRR_HISTORY);
        }
    }
    ret=GRR_RET_FILE_ACCESS;

    done:

    fclose(f);

    return ret;
}

bool readLine(FILE *f, char *destination, size_t size) {
    size_t len;

    if ( !fgets(destination,size,f) ) {
        return false;
    }
    len=strlen(destination);
    if ( len == 0 ) {
        return false;
    }

    destination[len-1]='\0';
    return true;
}

int searchDirectoryTree(DIR *dir, char *path, long *lineNo, const grrNfa nfa, const grrOptions *options) {
    int ret=GRR_RET_OK;
    size_t offset, newLen;
    struct dirent *entry;

    offset=strlen(path);

    while ( (entry=readdir(dir)) ) {
        struct stat file_stat;
        
        if ( entry->d_name[0] == '.' ) {
            if ( options->ignore_hidden || entry->d_name[1] == '\0'
                    || ( entry->d_name[1] == '.' && entry->d_name[2] == '\0' )
            ) {
                continue;
            }
        }

        path[offset]='\0';

        strncat(path,entry->d_name,GRR_PATH_MAX);
        newLen=strlen(path);

        if ( stat(path,&file_stat) != 0 ) {
            if ( options->verbose ) {
                fprintf(stderr,"Could not stat %s: %s\n", path, strerror(errno));
            }
            continue;
        }

        if ( S_ISREG(file_stat.st_mode) ) {
            if ( options->filePattern ) {
                if ( grrSearch(options->filePattern,entry->d_name,strlen(entry->d_name),NULL,NULL,NULL,0) != GRR_RET_OK ) {
                    continue;
                }
            }

            if ( searchFileForPattern(path,lineNo,nfa,options) == GRR_RET_BREAK_LOOP ) {
                ret=GRR_RET_BREAK_LOOP;
                goto done;
            }
        }
        else if ( S_ISDIR(file_stat.st_mode) ) {
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
        size_t len, start, end, offset, cursor;
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
        ret=grrSearch(nfa,line,len,&start,&end,&cursor,false);
        if ( ret == GRR_RET_BAD_DATA ) {
            if ( options->verbose ) {
                fprintf(stderr,"Terminating processing of %s since it contains non-printable data on line %zu, column %zu.\n", path, fileLineNo, cursor+1);
            }
            break;
        }

        if ( ret != GRR_RET_OK ) {
            continue;
        }

        (*lineNo)++;

        if ( options->editor ) {
            if ( (*lineNo)-1 == options->lineNo ) {
                executeEditor(options->editor,path,options->names_only? 1 : fileLineNo);
                ret=GRR_RET_BREAK_LOOP;
                break;
            }
            else if ( options->names_only ) {
                break;
            }
            continue;
        }

        if ( options->names_only ) {
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
        if ( !options->colorless ) {
            write(STDOUT_FILENO,changeColorToRed,sizeof(changeColorToRed));
        }
        if ( end-start > 50 ) {
            write(STDOUT_FILENO,line+start,10);
            write(STDOUT_FILENO," ... ",5);
            write(STDOUT_FILENO,line+end-10,10);
        }
        else {
            write(STDOUT_FILENO,line+start,end-start);
        }
        if ( !options->colorless ) {
            write(STDOUT_FILENO,restoreColor,sizeof(restoreColor));
        }

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

int executeEditor(const char *editor, const char *path, long lineNo) {
    int status;
    pid_t child;

    child=fork();
    switch ( child ) {
        case -1:
        perror("fork");
        return -1;

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

        perror(editor);
        exit(-2);

        default:
        while ( waitpid(child,&status,0) <= 0 );
        break;
    }

    if ( WIFSIGNALED(status) ) {
        return WTERMSIG(status)+1;
    }

    return WEXITSTATUS(status);
}
