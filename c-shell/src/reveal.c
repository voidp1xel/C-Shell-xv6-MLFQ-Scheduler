#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h> // The directory reading library
#include <unistd.h>
#include <sys/stat.h>
#include "../include/reveal.h"

//sort strings lexicographically
int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

//recursive function for -t traversal
void reveal_recursive(const char* absolute_path, const char* prefix, int a_flag, int t_flag){
    DIR *dir = opendir(absolute_path);
    if (dir == NULL){
        if (strlen(prefix) == 0) {
            printf("reveal: no such directory\n"); //if the very first folder we try to open does not exist or is empty, we can no longer recurse into folders. hence the file user is requesting doesnt exist.
        }
        return;
    }

    struct dirent *entry; //pointer to hold data of a file
    char *entries[4096]; //array to hold file names for sorting
    int count = 0; //no of saved files

    while ((entry = readdir(dir)) != NULL && count < 4096){
        if (!a_flag && entry->d_name[0] == '.') { //we dont need to read hidden files without a tag
            continue;
        }
        entries[count] = strdup(entry->d_name); //strdup duplicates a string and allocates memory for it
        count++;
    }
    closedir(dir);

    qsort(entries, count, sizeof(char *), compare_strings);

    for (int i = 0; i < count; i++){
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", absolute_path, entries[i]); //this is done to stitch the path and the file name for stat function later

        struct stat statbuf; //made to hold OS data about the file.
        int is_dir = 0; //assuming normal file be default
        if (stat(full_path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
            is_dir = 1; //we send the full path to OS. if it succeeds, we know that the file is a directory
        }

        int is_dot = (strcmp(entries[i], ".") == 0 || strcmp(entries[i], "..") == 0); //checks if the current folder is a relative link. done to prevent infinite looping.

        if (strlen(prefix) > 0) {
            printf("%s/%s%s\n", prefix, entries[i], (is_dir && !is_dot) ? "/" : ""); //correct syntax printing for directory and files.
        }
        else {
            printf("%s%s\n", entries[i], (is_dir && !is_dot) ? "/" : "");
        }

        if (t_flag && is_dir && !is_dot) { //if the user wants to dive into folders recursively.
            char new_prefix[4096];

            if (strlen(prefix) > 0) {
                snprintf(new_prefix, sizeof(new_prefix), "%s/%s", prefix, entries[i]); //building the prefix. eg we are in src folder and we need to go into include. the prefix now becomes src/include
            } 
            else {
                snprintf(new_prefix, sizeof(new_prefix), "%s", entries[i]);
            }
            //recurse into folders
            reveal_recursive(full_path, new_prefix, a_flag, t_flag);
        }
        free(entries[i]);
    }
}

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

    //recursive iterations through directory
    reveal_recursive(path_to_read, "", a_flag, t_flag);

    return 0;
}