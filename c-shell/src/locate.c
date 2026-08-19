#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/locate.h"

void locate_single_command(const char* command){
    int found = 0; //bool variable
    char path_buffer[4096]; //buffer to stitch directory and command - eg /usr/bin + / + python = usr/bin/python

    char cwd[4096];
    if(getcwd(cwd, sizeof(cwd)) != NULL){
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", cwd, command); //used to stitch together cwd. eg /home + / + aniket-> /home/aniket
        if(access(path_buffer, X_OK) == 0){ //checks file permissions, if R_OK then its readbale, if X_OK then exectuable
            printf("%s\n", path_buffer);
            found = 1;
        }
    }

    char* path_env = getenv("PATH");
    if(path_env != NULL){
        char* path_copy = strdup(path_env);  //duplicate of path_env. dynamic. clone in heap memory
        char* dir = strtok(path_copy, ":"); //string pointer holding current direcotry token extracted by strtok->string tokeniser. extract very first directory(everything upto first :)

        while(dir != NULL){ //stitching command names and checking if they exist and executable and then print em
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir, command);
            
            if(access(path_buffer, X_OK) == 0){
                printf("%s\n", path_buffer);
                found = 1;
            }

            dir = strtok(NULL, ":"); //means it tells strtok that resume from last string given until : found
        }
        free(path_copy);
    }

    if(!found){
        printf("locate: command not found (%s)\n", command);
    }
}

int execute_locate(char** targets, int target_count){
    for(int i = 0; i < target_count; i++){
        locate_single_command(targets[i]);
    }
    return 0;
}