#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/locate.h"

// Core function to find a specific executable[cite: 6].
void locate_single_command(const char* command){
    int found = 0; // Boolean flag[cite: 6].
    char path_buffer[4096]; 

    // RULE 1: First, check if the executable is sitting right here in the Current Working Directory[cite: 6].
    char cwd[4096];
    if(getcwd(cwd, sizeof(cwd)) != NULL){
        // Combine CWD and command (e.g., "/home/user" + "script.sh")[cite: 6].
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", cwd, command); 
        
        // 'access' with 'X_OK' asks the OS: "Does this file exist AND is it executable?"[cite: 6].
        if(access(path_buffer, X_OK) == 0){ 
            printf("%s\n", path_buffer); // Found it locally![cite: 6].
            found = 1;
        }
    }

    // RULE 2: Now, check all the system directories listed in the global $PATH environment variable[cite: 6].
    char* path_env = getenv("PATH"); // Retrieves a string like "/usr/bin:/bin:/usr/sbin"[cite: 6].
    
    if(path_env != NULL){
        // We duplicate the PATH string because 'strtok' modifies the string it works on[cite: 6].
        char* path_copy = strdup(path_env);  
        
        // 'strtok' splits a string by a delimiter (here, the colon ':')[cite: 6].
        // This isolates the first directory, e.g., "/usr/bin"[cite: 6].
        char* dir = strtok(path_copy, ":"); 

        // Keep looping until we run out of directories in the PATH[cite: 6].
        while(dir != NULL){ 
            // Append the command to the system directory (e.g., "/usr/bin/python")[cite: 6].
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir, command);
            
            // Check if it exists and is executable[cite: 6].
            if(access(path_buffer, X_OK) == 0){
                printf("%s\n", path_buffer); // Found it globally![cite: 6].
                found = 1;
            }
            
            // Calling strtok with NULL tells it to grab the *next* directory from our original string[cite: 6].
            dir = strtok(NULL, ":"); 
        }
        free(path_copy); // Prevent memory leaks[cite: 6].
    }

    // If we searched locally and through the entire PATH and still found nothing[cite: 6].
    if(!found){
        printf("locate: command not found (%s)\n", command);
    }
}

// Router function called by the parser[cite: 6].
int execute_locate(char** args, int arg_count){
    // Spec requirement: If they just type "locate" with no target, print a syntax error[cite: 6].
    if (arg_count <= 1) { 
        printf("locate: invalid syntax\n"); 
        return -1;
    }
    
    // Loop through every argument. e.g., 'locate python java' searches for both independently[cite: 6].
    for(int i = 1; i < arg_count; i++){
        locate_single_command(args[i]);
    }
    return 0; // Success[cite: 6].
}