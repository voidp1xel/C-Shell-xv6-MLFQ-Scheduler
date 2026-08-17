#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "../include/hop.h"

int execute_hop(char *target_pathway, char *home_directory, char *prev_directory){
    char curr_directory[4096];
    getcwd(curr_directory, sizeof(curr_directory)); //current location
    char go_to_path[4096] = ""; // acts as a buffer to build up commands. eg the chdir wont understand what ~ means and would locate a file named ~. thats why we make a temporary buffer.

    if(target_pathway == NULL || strcmp(target_pathway, "~") == 0){
        strcpy(go_to_path, home_directory);
    }

    else if(strcmp(target_pathway, "-") == 0){
        if(strlen(prev_directory) == 0){
            printf("cshell: hop: no previous directory\n"); //because at this point previous directory is just an empty string- there doesnt exist a previous of the start state
            return -1;
        }
        strcpy(go_to_path, prev_directory);
    }
    else if(target_pathway[0] == '~'){
        snprintf(go_to_path, sizeof(go_to_path), "%s%s", home_directory, target_pathway+1); //handles ~/Documents type of statements
    }

    else{
        strcpy(go_to_path, target_pathway);
    }

    if(chdir(go_to_path)!= 0){ //performs chdir function. if fails proceeds to throw error. also includes function hop.. as ch("..") is included in OS and it understands it as going back one heiracrchy of folder
        perror("cshell"); 
        return -1; //case where folder doesnt exist or where permission is denied
    }

    strcpy(prev_directory, curr_directory);

    char new_directory[4096];
    getcwd(new_directory, sizeof(new_directory));
    printf("%s\n", new_directory);

    return 0;
}