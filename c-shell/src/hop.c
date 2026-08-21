#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "../include/hop.h"

//helper function to read the persistent file and update the score of a successful hop
void update_frecency(const char* path, const char* home_directory) {
    char file_path[4096];
    snprintf(file_path, sizeof(file_path), "%s/.cshell_frecency", home_directory); //hidden file in home dir

    FILE* f = fopen(file_path, "r");
    char paths[1024][4096];
    int scores[1024];
    int count = 0;
    int found = 0;

    if (f) {
        char line[4096];
        while (fgets(line, sizeof(line), f) && count < 1024) { //read existing scores
            line[strcspn(line, "\n")] = 0; //strip newline
            char* comma = strrchr(line, ','); //find the comma separator
            if (comma) {
                *comma = '\0';
                strcpy(paths[count], line);
                scores[count] = atoi(comma + 1); //convert string score to int
                
                if (strcmp(paths[count], path) == 0) { //we found the directory we just hopped to!
                    scores[count]++; //boost its score
                    found = 1;
                }
                count++;
            }
        }
        fclose(f);
    }

    if (!found && count < 1024) { //brand new directory, add it with a score of 1
        strcpy(paths[count], path);
        scores[count] = 1;
        count++;
    }

    f = fopen(file_path, "w"); //overwrite file with updated scores
    if (f) {
        for (int i = 0; i < count; i++) {
            fprintf(f, "%s,%d\n", paths[i], scores[i]);
        }
        fclose(f);
    }
}

//helper function to find the highest ranked fuzzy match
int resolve_frecency(const char* target, const char* home_directory, char* best_match) {
    char file_path[4096];
    snprintf(file_path, sizeof(file_path), "%s/.cshell_frecency", home_directory);
    FILE* f = fopen(file_path, "r");
    if (!f) return 0; //no frecency history exists yet

    char line[4096];
    int highest_score = -1;
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        char* comma = strrchr(line, ',');
        if (comma) {
            *comma = '\0';
            int score = atoi(comma + 1);
            char* path = line; //everything before the comma is the path
            
            if (strstr(path, target) != NULL) { //target is a substring of this path!
                if (score > highest_score && access(path, F_OK) == 0) { //ensure it has the highest score AND still exists on disk
                    highest_score = score;
                    strcpy(best_match, path);
                    found = 1;
                }
            }
        }
    }
    fclose(f);
    return found;
}

int execute_hop(char** args, int arg_count, char *home_directory, char *prev_directory){
    if (arg_count == 1) { //just "hop" with no args. treat as "hop ~"
        args[1] = "~";
        arg_count = 2;
    }

    for (int i = 1; i < arg_count; i++) { //process chained hops sequentially
        char curr_directory[4096];
        getcwd(curr_directory, sizeof(curr_directory)); 
        char go_to_path[4096] = ""; 
        char* target_pathway = args[i];

        if(strcmp(target_pathway, "~") == 0){
            strcpy(go_to_path, home_directory);
        }
        else if(strcmp(target_pathway, "-") == 0){
            if(strlen(prev_directory) == 0){
                printf("cshell: hop: no previous directory\n"); 
                return -1;
            }
            strcpy(go_to_path, prev_directory);
        }
        else if(target_pathway[0] == '~'){
            snprintf(go_to_path, sizeof(go_to_path), "%s%s", home_directory, target_pathway+1); 
        }
        else{
            strcpy(go_to_path, target_pathway);
        }

        //try to change directory directly first
        if(chdir(go_to_path) != 0){ 
            //direct hop failed. fallback to frecency engine!
            char best_match[4096];
            if (resolve_frecency(target_pathway, home_directory, best_match)) {
                chdir(best_match); //frenecy found a match, hop to it
                strcpy(go_to_path, best_match); //update our tracking path
            } 
            else {
                printf("hop: no such directory\n"); 
                return -1;
            }
        }

        strcpy(prev_directory, curr_directory);
        char new_directory[4096];
        getcwd(new_directory, sizeof(new_directory));
        printf("%s\n", new_directory); //spec requirement: print the absolute path of the new directory

        //update frecency score, UNLESS the user typed "." which the spec says is a no-op
        if (strcmp(target_pathway, ".") != 0) {
            update_frecency(new_directory, home_directory);
        }
    }
    return 0;
}