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
#include <fcntl.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "engine/nfa.h"

#define GRR_VERSION "2.0.0"
#define GRR_PATH_MAX 4096
#define GRR_HISTORY ".grr_history"

#ifndef MIN
#define MIN(a,b) ( ( (a) < (b) )? (a) : (b) )
#endif

typedef struct grrOptions {
    char *starting_directory;
    char *editor;
    FILE *logger;
    grrNfa search_pattern;
    grrNfa file_pattern;
    long line_no;
    bool names_only;
    bool verbose;
    bool ignore_hidden;
    bool no_history;
    bool colorless;
} grrOptions;

typedef struct grrSimpleOptions {
    bool file_pattern;
    bool names_only;
    bool ignore_hidden;
} grrSimpleOptions;

int parseOptions(int argc, char **argv, grrOptions *options);
void usage(const char *executable);
int isExecutable(const char *path);
int compareOptionsToHistory(const grrOptions *options);
bool readLine(FILE *f, char *destination, size_t size);
int searchDirectoryTree(DIR *dir, char *path, long *line_no, const grrOptions *options);
int searchFileForPattern(const char *path, long *line_no, const grrOptions *options);
int executeEditor(const char *editor, const char *path, long line_no, bool verbose);

int main(int argc, char **argv) {
    int ret;
    long line_no;
    grrOptions options={0};
    char path[GRR_PATH_MAX];
    char tmp_file[]="./.grrtempXXXXXX";
    DIR *dir;

    options.starting_directory=path;

    ret=parseOptions(argc,argv,&options);
    if ( ret != GRR_RET_OK ) {
        if ( ret == GRR_RET_DONE ) {
            ret=GRR_RET_OK;
        }
        goto done;
    }

    if ( options.line_no >= 0 ) {
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

        if ( !options.no_history ) {
            ret=compareOptionsToHistory(&options);
            if ( ret == GRR_RET_OK ) {
                goto done;
            }
        }
    }
    else if ( !options.no_history ) {
        int fd;
        char starting_directory[GRR_PATH_MAX];

        options.editor=NULL;
        fd=mkstemp(tmp_file);
        if ( fd == -1 ) {
            if ( options.verbose ) {
                fprintf(stderr,"Failed to create create history file: %s\n", strerror(errno));
            }

            ret=GRR_RET_FILE_ACCESS;
            goto done;
        }

        options.logger=fdopen(fd,"wb");
        if ( !options.logger ) {
            if ( options.verbose ) {
                fprintf(stderr,"fdopen failed when creating history file: %s\n", strerror(errno));
            }

            close(fd);
            ret=GRR_RET_OUT_OF_MEMORY;
            goto done;
        }

        fprintf(options.logger,"%s\n", grrDescription(options.search_pattern));

        fd=open(path,O_RDONLY);
        if ( fd == -1 ) {
            if ( options.verbose ) {
                fprintf(stderr,"Failed to access starting directory: %s\n", strerror(errno));
            }

            ret=GRR_RET_FILE_ACCESS;
            goto done;
        }

        realpath(path,starting_directory);
        fprintf(options.logger,"%s\n", starting_directory);

        if ( options.file_pattern ) {
            fprintf(options.logger,"f");
        }
        if ( options.names_only ) {
            fprintf(options.logger,"n");
        }
        if ( options.ignore_hidden ) {
            fprintf(options.logger,"i");
        }
        fprintf(options.logger,"\n");

        if ( options.file_pattern ) {
            fprintf(options.logger,"%s\n", grrDescription(options.file_pattern));
        }
    }

    dir=opendir(path);
    if ( !dir ) {
        fprintf(stderr,"Failed to access starting directory.\n");
        ret=GRR_RET_FILE_ACCESS;
        goto done;
    }
    line_no=-1;
    searchDirectoryTree(dir,path,&line_no,&options);
    closedir(dir);

    done:

    grrFreeNfa(options.search_pattern);
    grrFreeNfa(options.file_pattern);
    if ( options.logger ) {
        fclose(options.logger);

        if ( ret == GRR_RET_OK ) {
            const char *home;

            home=getenv("HOME");
            if ( home ) {
                char new_path[GRR_PATH_MAX];

                snprintf(new_path,sizeof(new_path),"%s/%s", home, GRR_HISTORY);
                if ( rename(tmp_file,new_path) != 0 ) {
                    if ( options.verbose ) {
                        fprintf(stderr,"Failed to move the history file into the HOME directory: %s\n", strerror(errno));
                    }
                    unlink(tmp_file);
                }
            }
            else {
                if ( options.verbose ) {
                    fprintf(stderr,"Cannot save the history file because the HOME environment variable is unset.\n");
                }
                unlink(tmp_file);
            }
        }
    }

    return ret;
}

int parseOptions(int argc, char **argv, grrOptions *options) {
    int ret, optval;

    options->search_pattern=NULL;
    options->line_no=-1;
    sprintf(options->starting_directory,"./");

    if ( argc == 1 ) {
        usage(argv[0]);
        return GRR_RET_BAD_DATA;
    }

    while ( (optval=getopt(argc,argv,":r:d:f:e:l:niycvuh")) != -1 ) {
        struct stat file_stat;
        char *temp;

        switch ( optval ) {
            case 'r':
            ret=grrCompile(argv[optind-1],strlen(argv[optind-1]),&options->search_pattern);
            if ( ret != GRR_RET_OK ) {
                fprintf(stderr,"Could not compile pattern.\n");
                return ret;
            }
            break;

            case 'd':
            temp=argv[optind-1];
            if ( !temp[0] ) {
                fprintf(stderr,"Starting directory cannot be empty.\n");
                return GRR_RET_BAD_DATA;
            }
            if ( temp[strlen(temp)-1] == '/' ) {
                ret=snprintf(options->starting_directory,GRR_PATH_MAX,"%s", temp);
            }
            else {
                ret=snprintf(options->starting_directory,GRR_PATH_MAX,"%s/", temp);
            }
            if ( ret >= GRR_PATH_MAX ) {
                fprintf(stderr,"Starting directory is too long (max. of %i characters).\n", GRR_PATH_MAX-1);
                return GRR_RET_BAD_DATA;
            }

            if ( stat(options->starting_directory,&file_stat) != 0 ) {
                perror("Could not stat starting directory");
                return GRR_RET_FILE_ACCESS;
            }

            if ( !S_ISDIR(file_stat.st_mode) ) {
                fprintf(stderr,"%s is not a directory.\n", options->starting_directory);
                return GRR_RET_BAD_DATA;
            }

            if ( access(options->starting_directory,X_OK) != 0 ) {
                perror("Could not access starting directory");
                return GRR_RET_FILE_ACCESS;
            }
            break;

            case 'f':
            ret=grrCompile(optarg,strlen(optarg),&options->file_pattern);
            if ( ret != GRR_RET_OK ) {
                fprintf(stderr,"Could not compile file pattern.\n");
                return ret;
            }
            break;

            case 'e':
            options->editor=optarg;
            break;

            case 'l':
            errno=0;
            options->line_no=strtol(optarg,&temp,10);
            if ( temp == optarg || *temp || options->line_no < 0 || ( options->line_no == LONG_MAX && errno == ERANGE ) ) {
                fprintf(stderr,"Invalid 'l' option: %s\n", optarg);
                return GRR_RET_BAD_DATA;
            }
            break;

            case 'n':
            options->names_only=true;
            break;

            case 'i':
            options->ignore_hidden=true;
            break;

            case 'y':
            options->no_history=true;
            break;

            case 'c':
            options->colorless=true;
            break;

            case 'v':
            options->verbose=true;
            break;

            case 'u':
            printf("%s\n", GRR_VERSION);
            return GRR_RET_DONE;

            case 'h':
            usage(argv[0]);
            return GRR_RET_DONE;

            case '?':
            fprintf(stderr,"Invalid option: %c\n", optopt);
            usage(argv[0]);
            return GRR_RET_BAD_DATA;

            case ':':
            fprintf(stderr,"-%c requires an argument.\n", optopt);
            return GRR_RET_BAD_DATA;

            default:
            abort();
        }
    }

    if ( !options->search_pattern ) {
        fprintf(stderr,"No search pattern was provided.\n");
        usage(argv[0]);
        return GRR_RET_BAD_DATA;
    }

    return GRR_RET_OK;
}

void usage(const char *executable) {
    printf("Usage: %s [options]\n", executable);
    printf("Options:\n");
    printf("\t-r <pattern>        -- Specify the search regex.  Required unless either -u or -h are specified.\n");
    printf("\t-f <file-pattern>   -- Only examine files whose names (excluding the directory) match this regex.\n");
    printf("\t-e <editor>         -- When used in conjunction with -l, specifies the editor for opening files.\n");
    printf("\t                       Defaults to the EDITOR environment variable or vi/vim if that is unset.\n");
    printf("\t-l <result-number>  -- Opens up the file specified in the l^th result.\n");
    printf("\t-n                  -- Display only the file names and not the individual lines within them.\n");
    printf("\t-i                  -- Ignore hidden files and directories.\n");
    printf("\t-y                  -- Neither read from nor write to the history file.\n");
    printf("\t-c                  -- Don't color the output text.\n");
    printf("\t-v                  -- Print verbose output to stderr.\n");
    printf("\t-u                  -- Prints Grr's version.\n");
    printf("\t-h                  -- Prints this message.\n");
}

int isExecutable(const char *path) {
    char line[GRR_PATH_MAX];
    FILE *f;

    if ( access(path,X_OK) == 0 ) {
        return GRR_RET_OK;
    }

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
    int ret=GRR_RET_BAD_DATA;
    size_t len;
    const char *home;
    char history_file[50], line[GRR_PATH_MAX+10], absolute_starting_directory[GRR_PATH_MAX];
    FILE *f;
    grrSimpleOptions observed_options={0};

    home=getenv("HOME");
    if ( !home ) {
        if ( options->verbose ) {
            fprintf(stderr,"The HOME environment variable is unset.\n");
        }

        return GRR_RET_OTHER;
    }
    if ( snprintf(history_file,sizeof(history_file),"%s/%s", home, GRR_HISTORY) >= sizeof(history_file) ) {
        if ( options->verbose ) {
            fprintf(stderr,"%s/%s was too big for the buffer.\n", home, GRR_HISTORY);
        }

        return GRR_RET_OVERFLOW;
    }

    f=fopen(history_file,"rb");
    if ( !f ) {
        if ( options->verbose ) {
            fprintf(stderr,"Failed to open %s: %s\n", history_file, strerror(errno));
        }

        return GRR_RET_FILE_ACCESS;
    }

    if ( !readLine(f,line,sizeof(line)) ) {
        goto failed_read;
    }
    if ( strcmp(line,grrDescription(options->search_pattern)) != 0 ) {
        goto done;
    }

    if ( !readLine(f,line,sizeof(line)) ) {
        goto failed_read;
    }

    if ( !realpath(options->starting_directory,absolute_starting_directory) ) {
        if ( options->verbose ) {
            fprintf(stderr,"Failed to resolve absolute path of starting directory.\n");
        }

        ret=GRR_RET_OTHER;
        goto done;
    }

    if ( strcmp(line,absolute_starting_directory) != 0 ) {
        goto done;
    }

    if ( !readLine(f,line,sizeof(line)) ) {
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
                fprintf(stderr,"Skipping unsupported option: '%c'\n", line[k]);
            }
            break;
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
        if ( strcmp(line,grrDescription(options->file_pattern)) != 0 ) {
            goto done;
        }
    }
    else if ( options->file_pattern ) {
        goto done;
    }

    for (long k=0; k<=options->line_no; k++) {
        if ( !readLine(f,line,sizeof(line)) ) {
            goto failed_read;
        }
    }

    if ( observed_options.names_only ) {
        ret=executeEditor(options->editor,line,1,options->verbose);
    }
    else {
        char *colon_ptr, *temp;
        long line_no;

        colon_ptr=strstr(line,":");
        if ( !colon_ptr || colon_ptr[1] == '\0' ) {
            if ( options->verbose ) {
                fprintf(stderr,"No ':' found for result '%s' in %s.\n", line, history_file);
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
                fprintf(stderr,"Invalid line number found for result '%s' in %s.\n", line, history_file);
            }

            goto done;
        }

        ret=executeEditor(options->editor,line,line_no,options->verbose);
    }
    ret=( ret == 0 )? GRR_RET_OK : GRR_RET_EXEC;
    goto done;

    failed_read:

    if ( options->verbose ) {
        if ( ferror(f) ) {
            fprintf(stderr,"Failed to read from %s.\n", history_file);
        }
        else {
            fprintf(stderr,"Unexpected end of file found in %s.\n", history_file);
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

int searchDirectoryTree(DIR *dir, char *path, long *line_no, const grrOptions *options) {
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
            if ( options->file_pattern ) {
                if ( grrSearch(options->file_pattern,entry->d_name,strlen(entry->d_name),NULL,NULL,NULL,0) != GRR_RET_OK ) {
                    continue;
                }
            }

            if ( searchFileForPattern(path,line_no,options) == GRR_RET_DONE ) {
                ret=GRR_RET_DONE;
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

            ret=searchDirectoryTree(subdir,path,line_no,options);
            closedir(subdir);
            if ( ret == GRR_RET_DONE ) {
                goto done;
            }
            ret=GRR_RET_OK;
        }
    }

    done:

    path[offset]='\0';

    return ret;
}

int searchFileForPattern(const char *path, long *line_no, const grrOptions *options) {
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

    for (size_t file_line_no=1; fgets(line,sizeof(line),f); file_line_no++) {
        size_t len, start, end, offset, cursor;
        const char change_color_to_red[]={0x1b,'[','9','1','m','\0'};
        const char restore_color[]={0x1b,'[','0','m','\0'};

        len=strlen(line);
        if ( len > 0 && line[len-1] == '\n' ) {
            len--;
        }
        for(; len>0 && line[len-1]=='\r'; len--);
        if ( len == 0 ) {
            continue;
        }
        ret=grrSearch(options->search_pattern,line,len,&start,&end,&cursor,false);
        if ( ret == GRR_RET_BAD_DATA ) {
            if ( options->verbose ) {
                fprintf(stderr,"Terminating processing of %s since it contains non-printable data on line %zu, column %zu.\n", path, file_line_no, cursor+1);
            }
            break;
        }

        if ( ret != GRR_RET_OK ) {
            continue;
        }

        (*line_no)++;

        if ( options->editor ) {
            if ( *line_no == options->line_no ) {
                executeEditor(options->editor,path,options->names_only? 1 : file_line_no,options->verbose);
                ret=GRR_RET_DONE;
                break;
            }
            else if ( options->names_only ) {
                break;
            }
            continue;
        }

        if ( options->logger ) {
            fprintf(options->logger,"%s", path);
            if ( !options->names_only ) {
                fprintf(options->logger,":%li", file_line_no);
            }
            fprintf(options->logger,"\n");
        }

        if ( options->names_only ) {
            printf("(%li) %s\n", *line_no, path);
            break;
        }

        printf("(%li) %s (line %zu): ", *line_no, path, file_line_no);
        if ( start > 15 ) {
            printf("... ");
            offset=start-15;
        }
        else {
            offset=0;
        }

        printf("%.*s", (int)(start-offset), line+offset);
        if ( !options->colorless ) {
            printf("%s", change_color_to_red);
        }
        if ( end-start > 50 ) {
            printf("%.*s ... %.*s", 10, line+start, 10, line+end-10);
        }
        else {
            printf("%.*s", (int)(end-start), line+start);
        }
        if ( !options->colorless ) {
            printf("%s", restore_color);
        }

        if ( len-end > 15 ) {
            printf("%.*s ...\n", 15, line+end);
        }
        else {
            printf("%.*s\n", (int)(len-end), line+end);
        }
    }

    fclose(f);

    return ret;
}

int executeEditor(const char *editor, const char *path, long line_no, bool verbose) {
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

            if ( snprintf(argument,sizeof(argument),"+%li", line_no) >= sizeof(line_no) ) {
                if ( verbose ) {
                    fprintf(stderr,"line_no is too big to fit into buffer: %li\n", line_no);
                }
                exit(1);
            }
            if ( verbose ) {
                fprintf(stderr,"Executing: %s %s %s\n", editor, argument, path);
            }
            execlp(editor,editor,argument,path,NULL);
        }
        else {
            if ( verbose ) {
                fprintf(stderr,"Executing: %s %s\n", editor, path);
            }
            execlp(editor,editor,path,NULL);
        }

        perror(editor);
        exit(-2);

        default:
        while ( waitpid(child,&status,0) <= 0 );
        break;
    }

    if ( WIFSIGNALED(status) ) {
        if ( verbose ) {
            fprintf(stderr,"Child process was terminated by signal: %s\n", strsignal(WTERMSIG(status)));
        }
        return WTERMSIG(status)+1;
    }

    if ( verbose ) {
        fprintf(stderr,"Child exited with status %i\n", WEXITSTATUS(status));
    }
    return WEXITSTATUS(status);
}
