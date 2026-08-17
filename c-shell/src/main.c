#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include "../include/lexer.h"
#include "../include/hop.h"

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
    char prev_directory[MAXPATHLEN] = "";
    
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
        if (strlen(user_input) > 0) {
            token* token_list = make_token(user_input);

            
            //PARSER FUNCTION
            if (token_list != NULL) { 
                if(token_list->type == word_type && strcmp(token_list->value, "hop") == 0){
                    token* current_arg = token_list->next;
                    if (current_arg == NULL) { //just written hop in command. redirects as ~
                        execute_hop(NULL, home_directory, prev_directory);
                    }
                    else{ //chained commands
                        while (current_arg != NULL && current_arg->type == word_type){
                            execute_hop(current_arg->value, home_directory, prev_directory);
                            current_arg = current_arg->next;
                        }
                    }
                }

                clear_tokens(token_list);   
            }
        }

        free(user_input);

    }
    return 0;
}

