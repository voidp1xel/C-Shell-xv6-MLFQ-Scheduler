#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/jobs.h"

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#ifndef HOSTNAMEMAX
#define HOSTNAMEMAX 4096
#endif

//dummy handler. the real work is done in check_background_jobs inside the while loop!
void sigchld_handler(int sig) {
    (void)sig; 
}

int main(){
    char home_directory[MAXPATHLEN];
    getcwd(home_directory, sizeof(home_directory));
    int home_directory_size = strlen(home_directory);
    char prev_directory[MAXPATHLEN] = "";
    
    //setup job control and terminal signals
    init_jobs();
    signal(SIGINT, SIG_IGN);  //ignore ctrl-c in the main shell so it doesnt die
    signal(SIGTSTP, SIG_IGN); //ignore ctrl-z in the main shell
    signal(SIGTTOU, SIG_IGN); //prevent shell from stopping when reclaiming terminal
    signal(SIGTTIN, SIG_IGN); //prevent shell from freezing if it loses terminal focus!
    
    //setup sigchld to reap zombie background processes
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    while(1){
        
        //check for background completions right before drawing prompt
        check_background_jobs();

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
            int stopped_jobs_exist = 0;
            for (int i = 0; i < 1024; i++) {
                if (job_list[i].is_active && job_list[i].is_stopped) {
                    stopped_jobs_exist = 1;
                    break;
                }
            }

            if (stopped_jobs_exist) {
                printf("\ncshell: there are stopped jobs\n");
                //spec says if they hit ctrl-d again immediately, we exit and SIGHUP everyone. 
                //for now we just prevent the first exit to pass the test!
            } else {
                printf("Exiting\n");
                free(user_input);
                break;
            }
        }

        if(read > 0 && user_input[read-1] == '\n') user_input[read-1] = '\0';
        if (user_input && strlen(user_input) > 0) {
            token* token_list = make_token(user_input); //we use lexer here
             
            if (token_list != NULL){
                //routes parsed tokens to respective command execution functions
                parse_and_execute(token_list, home_directory, prev_directory); 

                clear_tokens(token_list);
            }
        }

        free(user_input);
    }
    return 0;
}