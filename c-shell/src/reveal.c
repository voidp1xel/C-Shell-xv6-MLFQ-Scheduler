#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../include/reveal.h"

static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/*
 * Traverses and lists directory contents alphabetically.
 * Supports hidden file display (-a) and recursive directory trees (-t).
 */
void reveal_recursive(const char* absolute_path, const char* prefix, int a_flag, int t_flag) {
    DIR *dir = opendir(absolute_path);
    if (dir == NULL) {
        if (strlen(prefix) == 0) printf("reveal: no such directory\n");
        return;
    }

    struct dirent *entry;
    int capacity = 64;
    char **entries = malloc(capacity * sizeof(char *));
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (!a_flag && entry->d_name[0] == '.') continue;

        if (count >= capacity) {
            capacity *= 2;
            entries = realloc(entries, capacity * sizeof(char *));
        }

        entries[count++] = strdup(entry->d_name);
    }
    closedir(dir);

    qsort(entries, count, sizeof(char *), compare_strings);

    for (int i = 0; i < count; i++) {
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", absolute_path, entries[i]);

        struct stat statbuf;
        int is_dir = 0;
        if (stat(full_path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
            is_dir = 1;
        }

        if (strlen(prefix) > 0) {
            printf("%s/%s%s\n", prefix, entries[i], is_dir ? "/" : "");
        } else {
            printf("%s%s\n", entries[i], is_dir ? "/" : "");
        }

        if (t_flag && is_dir) {
            char new_prefix[4096];
            if (strlen(prefix) > 0) {
                snprintf(new_prefix, sizeof(new_prefix), "%s/%s", prefix, entries[i]);
            } else {
                snprintf(new_prefix, sizeof(new_prefix), "%s", entries[i]);
            }
            reveal_recursive(full_path, new_prefix, a_flag, t_flag);
        }

        free(entries[i]);
    }
    free(entries);
}

/*
 * 'reveal' builtin: lists directory contents with options:
 * -a: include hidden files
 * -t: recursive tree traversal
 */
int execute_reveal(char** args, int arg_count, char* home_directory, char* prev_directory) {
    int a_flag = 0, t_flag = 0, invalid_syntax = 0;
    char* target_pathway = NULL;

    for (int i = 1; i < arg_count; i++) {
        if (args[i][0] == '-' && strlen(args[i]) > 1) {
            for (int j = 1; args[i][j] != '\0'; j++) {
                if (args[i][j] == 'a') a_flag = 1;
                else if (args[i][j] == 't') t_flag = 1;
                else invalid_syntax = 1;
            }
        } else {
            if (target_pathway != NULL) invalid_syntax = 1;
            else target_pathway = args[i];
        }
    }

    if (invalid_syntax) {
        printf("reveal: invalid syntax\n");
        return -1;
    }

    char path_to_read[4096] = "";

    if (target_pathway == NULL || strcmp(target_pathway, ".") == 0) {
        getcwd(path_to_read, sizeof(path_to_read));
    } else if (strcmp(target_pathway, "~") == 0) {
        strcpy(path_to_read, home_directory);
    } else if (strcmp(target_pathway, "-") == 0) {
        if (strlen(prev_directory) == 0) {
            printf("reveal: no such directory\n");
            return -1;
        }
        strcpy(path_to_read, prev_directory);
    } else if (target_pathway[0] == '~') {
        snprintf(path_to_read, sizeof(path_to_read), "%s%s", home_directory, target_pathway + 1);
    } else {
        strcpy(path_to_read, target_pathway);
    }

    reveal_recursive(path_to_read, "", a_flag, t_flag);
    return 0;
}