#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h> // POSIX directory manipulation (opendir, readdir)[cite: 5].
#include <unistd.h>
#include <sys/stat.h>
#include "../include/reveal.h"

// Helper function used by 'qsort' to sort an array of strings alphabetically[cite: 5].
int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b); // Standard string comparison[cite: 5].
}

// Core function to read and print the contents of a directory[cite: 5].
// 'absolute_path' is what the OS searches. 'prefix' is what we print to the user[cite: 5].
void reveal_recursive(const char* absolute_path, const char* prefix, int a_flag, int t_flag){
    
    // Open a directory stream[cite: 5].
    DIR *dir = opendir(absolute_path);
    if (dir == NULL){ // Fails if directory doesn't exist or we lack permissions[cite: 5].
        if (strlen(prefix) == 0) printf("reveal: no such directory\n"); 
        return;
    }

    struct dirent *entry; // Structure holding data about a single file inside the directory[cite: 5].
    
    // We cannot use a fixed-size array (like char arr[4096]) because a folder might have 10,000 files!
    // Instead, we use 'malloc' to create a dynamic array that can grow[cite: 5].
    int capacity = 64; // Start by making space for 64 file names[cite: 5].
    char **entries = malloc(capacity * sizeof(char *));
    int count = 0; 

    // Loop through every single file in the folder[cite: 5].
    while ((entry = readdir(dir)) != NULL){
        // Special case: ALWAYS ignore "." (current directory) and ".." (parent directory)[cite: 5].
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        // If the '-a' flag is OFF, ignore files starting with a dot (hidden files)[cite: 5].
        if (!a_flag && entry->d_name[0] == '.') continue; 

        // If our array is full, double its capacity using 'realloc' to prevent crashing[cite: 5].
        if (count >= capacity) { 
            capacity *= 2;
            entries = realloc(entries, capacity * sizeof(char *));
        }
        
        // Duplicate the filename and save it in our array[cite: 5].
        entries[count] = strdup(entry->d_name); 
        count++;
    }
    closedir(dir); // Close the stream[cite: 5].

    // Sort our dynamically allocated array alphabetically using C's built-in quicksort[cite: 5].
    qsort(entries, count, sizeof(char *), compare_strings);

    // Loop through the sorted array to print them out[cite: 5].
    for (int i = 0; i < count; i++){
        char full_path[4096];
        // Combine the folder path and filename so we can check its metadata (stat)[cite: 5].
        snprintf(full_path, sizeof(full_path), "%s/%s", absolute_path, entries[i]); 

        struct stat statbuf; 
        int is_dir = 0; 
        
        // Use 'stat' to determine if this specific entry is a file or another folder[cite: 5].
        if (stat(full_path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) is_dir = 1; 

        // Print the name. If it's a folder, append a '/' to the end[cite: 5].
        if (strlen(prefix) > 0) printf("%s/%s%s\n", prefix, entries[i], (is_dir) ? "/" : ""); 
        else printf("%s%s\n", entries[i], (is_dir) ? "/" : "");

        // --- RECURSIVE LOGIC ---
        // If the user passed '-t' (tree/recursive flag) AND this entry is a folder, dive into it[cite: 5].
        if (t_flag && is_dir) { 
            char new_prefix[4096];
            // Build the new visual prefix for the user[cite: 5].
            if (strlen(prefix) > 0) snprintf(new_prefix, sizeof(new_prefix), "%s/%s", prefix, entries[i]); 
            else snprintf(new_prefix, sizeof(new_prefix), "%s", entries[i]);
            
            // Call THIS exact function again, but aimed at the new subfolder[cite: 5].
            reveal_recursive(full_path, new_prefix, a_flag, t_flag);
        }
        
        // Free the memory we allocated for the string[cite: 5].
        free(entries[i]); 
    }
    // Free the main array wrapper[cite: 5].
    free(entries); 
}

// Router function called by the parser[cite: 5].
int execute_reveal(char** args, int arg_count, char* home_directory, char* prev_directory){
    int a_flag = 0, t_flag = 0, invalid_syntax = 0;
    char* target_pathway = NULL;

    // Parse arguments for flags (-a, -t) and the target folder name[cite: 5].
    for (int i = 1; i < arg_count; i++) { 
        if (args[i][0] == '-' && strlen(args[i]) > 1) {
            for (int j = 1; args[i][j] != '\0'; j++) {
                if (args[i][j] == 'a') a_flag = 1;
                else if (args[i][j] == 't') t_flag = 1;
                else invalid_syntax = 1;
            }
        } 
        else {
            // If they provided multiple folder names, it's a syntax error for reveal[cite: 5].
            if (target_pathway != NULL) invalid_syntax = 1; 
            else target_pathway = args[i];
        }
    }

    if (invalid_syntax) {
        printf("reveal: invalid syntax\n");
        return -1;
    }

    char path_to_read[4096] = "";

    // Resolve what directory the user actually wants us to read[cite: 5].
    // If no path is provided, or ".", use the current working directory[cite: 5].
    if (target_pathway == NULL || strcmp(target_pathway, ".") == 0) getcwd(path_to_read, sizeof(path_to_read)); 
    // If "~", resolve it to the shell's original home directory[cite: 5].
    else if (strcmp(target_pathway, "~") == 0) strcpy(path_to_read, home_directory); 
    // If "-", jump back to the previously visited directory[cite: 5].
    else if (strcmp(target_pathway, "-") == 0) {
        if (strlen(prev_directory) == 0) { // Failsafe if they haven't hopped anywhere yet[cite: 5].
            printf("reveal: no such directory\n");
            return -1;
        }
        strcpy(path_to_read, prev_directory);
    }
    // If path starts with "~/" (e.g., "~/Downloads"), replace the ~ with the absolute home path[cite: 5].
    else if (target_pathway[0] == '~') snprintf(path_to_read, sizeof(path_to_read), "%s%s", home_directory, target_pathway + 1); 
    // Otherwise, it's a standard absolute or relative path[cite: 5].
    else strcpy(path_to_read, target_pathway); 

    // Kick off the directory reading process[cite: 5].
    reveal_recursive(path_to_read, "", a_flag, t_flag);
    return 0;
}