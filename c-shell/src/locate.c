#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/locate.h"

/*
 * Searches for all instances of an executable name across the current
 * working directory and all directories in the $PATH environment variable.
 */
void locate_single_command(const char* command) {
    int found = 0;
    char path_buffer[4096];

    /* Check current working directory */
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", cwd, command);
        if (access(path_buffer, X_OK) == 0) {
            printf("%s\n", path_buffer);
            found = 1;
        }
    }

    /* Check system PATH entries */
    char* path_env = getenv("PATH");
    if (path_env != NULL) {
        char* path_copy = strdup(path_env);
        char* dir = strtok(path_copy, ":");

        while (dir != NULL) {
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir, command);
            if (access(path_buffer, X_OK) == 0) {
                printf("%s\n", path_buffer);
                found = 1;
            }
            dir = strtok(NULL, ":");
        }
        free(path_copy);
    }

    if (!found) {
        printf("locate: command not found (%s)\n", command);
    }
}

/*
 * 'locate' builtin: finds executables in CWD and PATH for one or more commands.
 */
int execute_locate(char** args, int arg_count) {
    if (arg_count <= 1) {
        printf("locate: invalid syntax\n");
        return -1;
    }

    for (int i = 1; i < arg_count; i++) {
        locate_single_command(args[i]);
    }
    return 0;
}