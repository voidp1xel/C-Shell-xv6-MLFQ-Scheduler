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

/* Dummy handler for SIGCHLD to ensure interrupted system calls are awakened */
void sigchld_handler(int sig) {
    (void)sig;
}

int main(void) {
    char home_directory[MAXPATHLEN];
    if (getcwd(home_directory, sizeof(home_directory)) == NULL) {
        perror("cshell: getcwd");
        return 1;
    }
    int home_directory_size = strlen(home_directory);
    char prev_directory[MAXPATHLEN] = "";

    init_jobs();

    /* Ignore interactive signals in the parent shell */
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    /* Setup SIGCHLD to reap children without stopping on pauses */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    while (1) {
        /* Check for any background job status changes */
        check_background_jobs();

        /* Construct shell prompt */
        char* username = getenv("USER");
        if (username == NULL) username = "user";

        char hostname[HOSTNAMEMAX];
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            strncpy(hostname, "localhost", sizeof(hostname) - 1);
            hostname[sizeof(hostname) - 1] = '\0';
        }

        char cwd[MAXPATHLEN];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("cshell: getcwd");
            continue;
        }

        /* Replace home directory prefix with ~ */
        if (strncmp(cwd, home_directory, home_directory_size) == 0) {
            printf("<%s@%s:~%s> ", username, hostname, cwd + home_directory_size);
        } else {
            printf("<%s@%s:%s> ", username, hostname, cwd);
        }
        fflush(stdout);

        char* user_input = NULL;
        size_t length = 0;
        ssize_t read = getline(&user_input, &length, stdin);

        if (read == -1) {
            /* Check if any suspended background jobs prevent exit */
            int stopped_jobs_exist = 0;
            for (int i = 0; i < 1024; i++) {
                if (job_list[i].is_active && job_list[i].is_stopped) {
                    stopped_jobs_exist = 1;
                    break;
                }
            }

            if (stopped_jobs_exist) {
                printf("\ncshell: there are stopped jobs\n");
            } else {
                printf("Exiting\n");
                free(user_input);
                break;
            }
        }

        if (read > 0 && user_input[read - 1] == '\n') {
            user_input[read - 1] = '\0';
        }

        if (user_input && strlen(user_input) > 0) {
            token* token_list = make_token(user_input);
            if (token_list != NULL) {
                parse_and_execute(token_list, home_directory, prev_directory);
                clear_tokens(token_list);
            }
        }

        free(user_input);
    }

    return 0;
}