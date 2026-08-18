#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h> // The directory reading library
#include <unistd.h>
#include "../include/reveal.h"

int execute_reveal(int a_flag, int t_flag, char* target_pathway, char* home_directory, char* prev_directory){
    char path_to_read[4096] = "";

    if (target_pathway == NULL || strcmp(target_pathway, ".") == 0){
        getcwd(path_to_read, sizeof(path_to_read)); // Default to current directory if no arg
    }
    else if (strcmp(target_pathway, "~") == 0) {
        strcpy(path_to_read, home_directory); //redirect to home directory. eg reveal ~
    }
    else if (strcmp(target_pathway, "-") == 0) {
        if (strlen(prev_directory) == 0) { //if reveal -, and there exists no previous directory.
            printf("reveal: no such directory\n");
            return -1;
        }
        strcpy(path_to_read, prev_directory);//previous directory exists.
    }
    else if (target_pathway[0] == '~') {
        snprintf(path_to_read, sizeof(path_to_read), "%s%s", home_directory, target_pathway + 1); //case of relative path given. eg reveal ~/src
    }
    else {
        strcpy(path_to_read, target_pathway); //regular reveal command without using any shorthand notations or shortcuts
    }

    return 0;
}