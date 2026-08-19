#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h> 
#include "../include/execute.h"

int resolve_command_path(const char* command, char* resolved_path) {
    char cwd[4096];
    char path_buffer[4096];

    if(strchr(command, '/') != NULL){ //returns pointer to first instance of character in string
        if(access(command, X_OK) == 0){ //if the user types ./build.sh or /bin/ls, strchr checks for /. if it exists we test its permissions
            strcpy(resolved_path, command);
            return 1; //success
        }
        return 0;
    }

    int skip_cwd = 0;
    const char* actual_command = command;
    
    if(command[0] == '%'){ //if we see a %, it skips pointer one forwad. eg %ls. we check only ls and skip %
        skip_cwd = 1;
        actual_command = command + 1;
    }

    if(!skip_cwd && getcwd(cwd, sizeof(cwd)) != NULL){ //if not conatin %, we stitch cwd and command and check for permissions
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", cwd, actual_command);
        if(access(path_buffer, X_OK) == 0){
            strcpy(resolved_path, path_buffer);
            return 1;
        }
    }

    char* path_env = getenv("PATH"); //asks os for value of path variable.  The OS returns a pointer to a massive string that looks like this: /usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin. It is a list of directories separated by colons (:).
    if(path_env != NULL){
        char* path_copy = strdup(path_env); //copy required bcs strtok modifies the original string
        char* dir = strtok(path_copy, ":"); //dir now points to just the first directory, for example: /usr/local/bin

        while(dir != NULL){
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir, actual_command);
            
            if(access(path_buffer, X_OK) == 0){
                strcpy(resolved_path, path_buffer);
                free(path_copy);
                return 1;
            }
            dir = strtok(NULL, ":");
        }
        free(path_copy);
    }

    return 0;
}

int execute_external_command(char** args) {
    char resolved_path[4096];

    if(!resolve_command_path(args[0], resolved_path)){
        printf("cshell: command not found (%s)\n", args[0]);
        return -1;
    }

    pid_t pid = fork(); //needed to hold id of running process

    if(pid < 0){
        perror("cshell");
        return -1;
    }

    else if(pid == 0){ //inside child fork as pid = 0
        if(args[0][0] == '%'){ //if user types %ls or sum like that, we move pointer forward to make it think user type ls
            args[0] = args[0] + 1;
        }

        execv(resolved_path, args); //exec command called. if it works, child process is replaced by new data

        perror("cshell"); //since child did not get replaced by new data, we knew the exec command failed
        exit(EXIT_FAILURE);
    }

    else{
        int status;
        waitpid(pid, &status, 0); //wait until process with id = pid terminates
    }

    return 0;
}