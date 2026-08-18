#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include "../include/lexer.h"
#include "../include/hop.h"
#include "../include/reveal.h"

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

                //PARSER FOR HOP
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

                //PARSER FOR REVEAL
                else if(token_list->type == word_type && strcmp(token_list->value, "reveal") == 0){
                    int a_flag = 0;
                    int t_flag = 0;
                    char* target = NULL;
                    int invalid_syntax = 0;

                    token* current_arg = token_list->next;

                    while (current_arg != NULL && current_arg->type == word_type){
                        if (current_arg->value[0] == '-' && strlen(current_arg->value) > 1){ // if user types something like reveal -att
                            for (int i = 1; current_arg->value[i] != '\0'; i++) { //checks syntax and keeps flags
                                if (current_arg->value[i] == 'a') a_flag = 1;
                                else if (current_arg->value[i] == 't') t_flag = 1;
                                else invalid_syntax = 1;
                            }
                        }
                        else{ //if reveal command didnt start with '-' or if it was exactly reveal -. this implies it must talk about src or ~
                            if (target != NULL) {
                                invalid_syntax = 1; //if target is already filled with previous command. reveal cant take 2 targets. eg reveal src include.
                            }
                            else {
                                target = current_arg->value; //eg reveal include
                            }
                        }
                        current_arg = current_arg->next;
                    }
                    if (invalid_syntax) {
                        printf("reveal: invalid syntax\n");
                    }
                    else {
                        execute_reveal(a_flag, t_flag, target, home_directory, prev_directory);
                    }
                }

                clear_tokens(token_list);   
            }
        }

        free(user_input);

    }
    return 0;
}

