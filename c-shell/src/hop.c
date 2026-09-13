#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "../include/hop.h"

// Helper function to update the ".cshell_frecency" tracker file every time you successfully change directories.
void update_frecency(const char* path, const char* home_directory) {
    char file_path[4096];
    // Create the path to our hidden tracking file in the root of the shell directory.
    snprintf(file_path, sizeof(file_path), "%s/.cshell_frecency", home_directory); 

    FILE* f = fopen(file_path, "r");
    char paths[1024][4096]; // Arrays to hold data loaded from the file.
    int scores[1024];
    int count = 0;
    int found = 0;

    // Phase 1: Read existing scores from the CSV file.
    if (f) {
        char line[4096];
        while (fgets(line, sizeof(line), f) && count < 1024) { 
            line[strcspn(line, "\n")] = 0; // Strip the newline character off the end.
            char* comma = strrchr(line, ','); // Find the comma separating the path and the score.
            if (comma) {
                *comma = '\0'; // Replace comma with null terminator to split the string in two.
                strcpy(paths[count], line);
                scores[count] = atoi(comma + 1); // Convert the string number after the comma to an integer.
                
                if (strcmp(paths[count], path) == 0) { // We found the directory we just hopped to!
                    scores[count]++; // Boost its frequency score.
                    found = 1;
                }
                count++;
            }
        }
        fclose(f);
    }

    // Phase 2: If we've never visited this directory before, add it to our array with a score of 1.
    if (!found && count < 1024) { 
        strcpy(paths[count], path);
        scores[count] = 1;
        count++;
    }

    // Phase 3: Open the file in "write" (w) mode, which overwrites everything, and dump our updated array.
    f = fopen(file_path, "w"); 
    if (f) {
        for (int i = 0; i < count; i++) {
            fprintf(f, "%s,%d\n", paths[i], scores[i]); // Save in "path,score" CSV format.
        }
        fclose(f);
    }
}

// Helper function to scan the frecency file when the user types a partial path.
int resolve_frecency(const char* target, const char* home_directory, char* best_match) {
    char file_path[4096];
    snprintf(file_path, sizeof(file_path), "%s/.cshell_frecency", home_directory);
    FILE* f = fopen(file_path, "r");
    if (!f) return 0; // The file hasn't been created yet.

    char line[4096];
    int highest_score = -1;
    int found = 0;

    // Scan through every logged directory.
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        char* comma = strrchr(line, ',');
        if (comma) {
            *comma = '\0';
            int score = atoi(comma + 1);
            char* path = line; 
            
            // strstr checks if 'target' is a substring inside 'path' (e.g., "osn" inside "/home/osnmp1").
            if (strstr(path, target) != NULL) { 
                // We found a match! Check if its score beats the current high score.
                // We also use access() with F_OK to ensure the directory wasn't deleted by the user!
                if (score > highest_score && access(path, F_OK) == 0) { 
                    highest_score = score;
                    strcpy(best_match, path);
                    found = 1;
                }
            }
        }
    }
    fclose(f);
    return found; // Returns 1 if a fuzzy match was found, 0 if nothing matched.
}

int execute_hop(char** args, int arg_count, char *home_directory, char *prev_directory){
    if (arg_count == 1) { // If they just typed "hop", default to "hop ~"
        args[1] = "~";
        arg_count = 2;
    }

    // Process chained hops sequentially (e.g., "hop dir1 dir2 dir3").
    for (int i = 1; i < arg_count; i++) { 
        char curr_directory[4096];
        getcwd(curr_directory, sizeof(curr_directory)); // Save where we are before we jump.
        char go_to_path[4096] = ""; 
        char* target_pathway = args[i];

        // Evaluate special shell path tokens.
        if(strcmp(target_pathway, "~") == 0){
            strcpy(go_to_path, home_directory);
        }
        else if(strcmp(target_pathway, "-") == 0){ // Jump back to previous directory.
            if(strlen(prev_directory) == 0){
                printf("cshell: hop: no previous directory\n"); 
                return -1;
            }
            strcpy(go_to_path, prev_directory);
        }
        else if(target_pathway[0] == '~'){ // E.g., "~/Documents"
            snprintf(go_to_path, sizeof(go_to_path), "%s%s", home_directory, target_pathway+1); 
        }
        else{
            strcpy(go_to_path, target_pathway); // Absolute or relative path.
        }

        // Attempt to execute a normal directory change.
        if(chdir(go_to_path) != 0){ 
            // Normal hop failed. Time to ask the frecency engine if it recognizes the string!
            char best_match[4096];
            if (resolve_frecency(target_pathway, home_directory, best_match)) {
                chdir(best_match); // Frecency found a match, jump to it!
                strcpy(go_to_path, best_match); // Update our tracking variable.
            } 
            else {
                printf("hop: no such directory\n"); 
                return -1;
            }
        }

        // Successfully changed directories! Update our memory of where we just were.
        strcpy(prev_directory, curr_directory);
        
        char new_directory[4096];
        getcwd(new_directory, sizeof(new_directory));
        printf("%s\n", new_directory); // Rubric requirement: print the absolute path of the new destination.

        // Update the frecency score tracker, UNLESS the user typed "." (stay here), which is a no-op.
        if (strcmp(target_pathway, ".") != 0) {
            update_frecency(new_directory, home_directory);
        }
    }
    return 0;
}