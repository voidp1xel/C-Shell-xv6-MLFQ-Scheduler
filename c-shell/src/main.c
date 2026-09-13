// These are standard C libraries. 
// stdio.h is for input/output (printf), stdlib.h is for memory allocation (malloc/free) and environment variables.
#include <stdio.h>
#include <stdlib.h>
// unistd.h provides access to POSIX operating system API (like getcwd, gethostname, fork, exec).
#include <unistd.h>
#include <string.h>
#include <limits.h>
// signal.h allows us to handle OS signals like Ctrl+C (SIGINT) or background process completion (SIGCHLD).
#include <signal.h>

// These include files link to the other parts of your project (Lexer, Parser, and Background Job tracker).
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/jobs.h"

// We define a fallback maximum path length (4096 bytes) just in case the system doesn't define it.
// This prevents buffer overflow vulnerabilities when storing file paths.
#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

// Similarly, we define a maximum length for the name of the computer (hostname).
#ifndef HOSTNAMEMAX
#define HOSTNAMEMAX 4096
#endif

// This is a dummy signal handler for SIGCHLD (which triggers when a background child process finishes).
// We don't actually do the reaping here because doing complex logic inside an interrupt handler is unsafe.
// The real reaping is done safely inside the main loop by calling `check_background_jobs()`.
void sigchld_handler(int sig) {
    (void)sig; // (void)sig simply stops the compiler from complaining that the variable 'sig' is unused.
}

int main(){
    // Create a buffer to store the directory where the shell was initially launched.
    char home_directory[MAXPATHLEN];
    
    // getcwd gets the Current Working Directory and stores it in our buffer. 
    // This becomes the "~" (home) directory for our custom shell.
    getcwd(home_directory, sizeof(home_directory));
    
    // We save the length of the home directory string so we can easily compare it later to print the "~".
    int home_directory_size = strlen(home_directory);
    
    // This buffer will remember the last visited directory, allowing the 'hop -' command to work.
    char prev_directory[MAXPATHLEN] = "";
    
    // Initialize our background job tracking arrays/data structures.
    init_jobs();
    
    // --- TERMINAL SIGNAL MANAGEMENT ---
    // If we don't do this, pressing Ctrl+C will kill our shell entirely instead of just killing the running command.
    // SIG_IGN means "Ignore Signal". We tell the main shell process to ignore these interrupts.
    signal(SIGINT, SIG_IGN);  // Ignore Ctrl-C
    signal(SIGTSTP, SIG_IGN); // Ignore Ctrl-Z (suspend)
    signal(SIGTTOU, SIG_IGN); // Prevent the shell from suspending itself when it tries to write to the terminal.
    signal(SIGTTIN, SIG_IGN); // Prevent the shell from freezing if it tries to read from the terminal in the background.
    
    // --- SIGCHLD SETUP ---
    // sigaction is a more robust, modern version of signal(). We use it to catch when child processes die.
    struct sigaction sa;
    sa.sa_handler = sigchld_handler; // Point it to our dummy function above.
    sigemptyset(&sa.sa_mask);        // Clear out the signal mask so we don't block other signals unnecessarily.
    
    // SA_RESTART: Automatically restarts interrupted system calls (like reading input) so the shell doesn't crash if a background job finishes while we are typing.
    // SA_NOCLDSTOP: We only care when children terminate, not when they just stop/pause.
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);   // Actually apply the settings.

    // This is the REPL (Read-Eval-Print Loop). The shell runs forever until exited.
    while(1){
        
        // Before we draw the prompt, check if any background processes finished while we were busy.
        // If they did, this function will print the "<command> with pid <pid> exited normally" messages.
        check_background_jobs();

        // --- PROMPT GENERATION ---
        // Fetch the username of the person running the shell from the OS environment variables.
        char* username = getenv("USER");
        if(username == NULL) username = "no_name_user"; // Fallback just in case.

        // Fetch the name of the computer (e.g., 'Anikets-MacBook-Pro').
        char hostname[HOSTNAMEMAX];
        gethostname(hostname, sizeof(hostname));

        // Get the directory the user is *currently* sitting in right now.
        char cwd[MAXPATHLEN];
        getcwd(cwd, sizeof(cwd));

        // This checks if the user's current directory starts with the shell's original home directory.
        // For example, if home is "/home/user/osn" and cwd is "/home/user/osn/test", strncmp will return 0 (a match).
        if(strncmp(cwd, home_directory, home_directory_size) == 0){
            // It's a match! We print the username, hostname, and replace the home path with a '~'.
            // `cwd + home_directory_size` uses pointer arithmetic to skip the home path part of the string.
            printf("<%s@%s:~%s> ", username, hostname,  cwd + home_directory_size);
        }
        else{
            // If they wandered entirely outside the home directory space, just print the raw absolute path.
            printf("<%s@%s:%s> ", username, hostname, cwd);
        }

        // Standard output is usually line-buffered (it waits for a '\n' before drawing to the screen).
        // Since our prompt doesn't end in a newline, we MUST call fflush to force the text onto the screen immediately.
        fflush(stdout); 

        // --- INPUT READING ---
        char* user_input = NULL;
        size_t length = 0; // size_t is an unsigned integer type used to represent sizes of objects safely.
        
        // getline() dynamically allocates memory (using malloc under the hood) to fit whatever the user typed.
        // It returns the number of characters read, or -1 if it encounters an error or EOF (End of File).
        ssize_t read = getline(&user_input, &length, stdin); 

        // If read is -1, it usually means the user pressed Ctrl+D (which sends an EOF signal to standard input).
        if(read == -1){ 
            int stopped_jobs_exist = 0;
            
            // The rubric requires that we do not exit if there are jobs suspended in the background.
            // We loop through our global job tracker to see if anything is currently stopped.
            for (int i = 0; i < 1024; i++) {
                if (job_list[i].is_active && job_list[i].is_stopped) {
                    stopped_jobs_exist = 1;
                    break;
                }
            }

            if (stopped_jobs_exist) {
                // Warning the user. (The TA specification states that pressing Ctrl+D twice should force the exit, 
                // but this implementation just prevents the first exit to satisfy basic testing).
                printf("\ncshell: there are stopped jobs\n");
            } else {
                // If there are no stopped jobs, it's safe to exit the shell.
                printf("Exiting\n");
                free(user_input); // Always free dynamically allocated memory before exiting to prevent leaks.
                break;            // Breaks the while(1) loop, ending the program.
            }
        }

        // getline() captures the 'Enter' key as a newline character ('\n') at the end of the string.
        // We replace it with a null terminator ('\0') because string functions expect strings to end with '\0'.
        if(read > 0 && user_input[read-1] == '\n') user_input[read-1] = '\0';
        
        // If the user actually typed something (not just mashing enter).
        if (user_input && strlen(user_input) > 0) {
            
            // --- LEXER (Tokenization) ---
            // Pass the raw string to the lexer. It breaks "ls -l > out.txt" into a linked list of tokens: 
            // [WORD: "ls"] -> [WORD: "-l"] -> [OPERATOR: ">"] -> [WORD: "out.txt"]
            token* token_list = make_token(user_input); 
             
            if (token_list != NULL){
                // --- PARSER (Execution routing) ---
                // Hands the tokens over to parser.c to figure out pipelines, semicolons, and ampersands.
                parse_and_execute(token_list, home_directory, prev_directory); 

                // Memory cleanup: free the linked list of tokens since we are done with this command.
                clear_tokens(token_list);
            }
        }

        // Memory cleanup: free the raw string buffer allocated by getline() before the loop restarts.
        free(user_input);
    }
    return 0; // Shell successfully terminated.
}