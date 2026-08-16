#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include "../include/lexer.h"

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#ifndef HOSTNAMEMAX
#define HOSTNAMEMAX 4096
#endif

int main(){
    char home_directory[MAXPATHLEN];
    getcwd(home_directory, sizeof(home_directory));
    int home_directory_size = strlen(home_directory);

    while(1){
        //home directory function
        char* username = getenv("USER");
        if(username == NULL) username = "no_name_user";

        char hostname[HOSTNAMEMAX];
        gethostname(hostname, sizeof(hostname));

        char cwd[MAXPATHLEN];
        getcwd(cwd, sizeof(cwd));

        if(strncmp(cwd, home_directory, home_directory_size) == 0){
            printf("<%s@%s:~%s> ", username, hostname,  cwd + home_directory_size);//compares string-> if home directory found then doesnt output '~'
        }
        else{
            printf("<%s@%s:%s> ", username, hostname, cwd);
        }

        fflush(stdout); //prints outpout even if \n is not present->forces to print

        //user input starts

        char* user_input = NULL;
        size_t length = 0; //used size_t bcs int is limited to 32 bits, if we want to open large file then this is the way. size_t dynamically adabts to bits your system can use
        ssize_t read = getline(&user_input, &length, stdin); //knows and calculates exact size

        if(read == -1){ //Ctrl+D situation
            printf("Exiting\n");
            free(user_input);
            break;
        }

        if(user_input[read-1] == '\n') user_input[read-1] = '\0';

        free(user_input);

    }
    return 0;
}

