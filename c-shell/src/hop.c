#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../include/hop.h"

/*
 * Updates directory frecency database (~/.cshell_frecency).
 * Tracks visit count and a logical clock timestamp for recency decay.
 */
void update_frecency(const char* path, const char* home_directory) {
    char file_path[4096];
    snprintf(file_path, sizeof(file_path), "%s/.cshell_frecency", home_directory);

    FILE* f = fopen(file_path, "r");
    char paths[1024][4096];
    int visits[1024];
    int last_hop_time[1024];

    int global_clock = 0;
    int count = 0;
    int found = 0;

    if (f) {
        char line[4096];
        if (fgets(line, sizeof(line), f)) {
            global_clock = atoi(line);
        }

        while (fgets(line, sizeof(line), f) && count < 1024) {
            line[strcspn(line, "\n")] = 0;

            char* comma1 = strchr(line, ',');
            char* comma2 = comma1 ? strchr(comma1 + 1, ',') : NULL;

            if (comma1 && comma2) {
                *comma1 = '\0';
                *comma2 = '\0';

                strcpy(paths[count], line);
                visits[count] = atoi(comma1 + 1);
                last_hop_time[count] = atoi(comma2 + 1);

                if (strcmp(paths[count], path) == 0) {
                    visits[count]++;
                    last_hop_time[count] = global_clock + 1;
                    found = 1;
                }
                count++;
            }
        }
        fclose(f);
    }

    global_clock++;

    if (!found && count < 1024) {
        strcpy(paths[count], path);
        visits[count] = 1;
        last_hop_time[count] = global_clock;
        count++;
    }

    f = fopen(file_path, "w");
    if (f) {
        fprintf(f, "%d\n", global_clock);
        for (int i = 0; i < count; i++) {
            fprintf(f, "%s,%d,%d\n", paths[i], visits[i], last_hop_time[i]);
        }
        fclose(f);
    }
}

/*
 * Resolves target substring to the highest ranking directory using
 * frecency scoring: rank = frequency * recency_weight(age).
 */
int resolve_frecency(const char* target, const char* home_directory, char* best_match) {
    char file_path[4096];
    snprintf(file_path, sizeof(file_path), "%s/.cshell_frecency", home_directory);

    FILE* f = fopen(file_path, "r");
    if (!f) return 0;

    char line[4096];
    int global_clock = 0;
    int highest_rank = -1;
    int found = 0;

    if (fgets(line, sizeof(line), f)) {
        global_clock = atoi(line);
    }

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;

        char* comma1 = strchr(line, ',');
        char* comma2 = comma1 ? strchr(comma1 + 1, ',') : NULL;

        if (comma1 && comma2) {
            *comma1 = '\0';
            *comma2 = '\0';

            char* path = line;
            int visits = atoi(comma1 + 1);
            int last_hop = atoi(comma2 + 1);

            if (strstr(path, target) != NULL) {
                if (access(path, F_OK) == 0) {
                    int age = global_clock - last_hop;
                    int weight = 1;

                    if (age <= 3) weight = 4;
                    else if (age <= 10) weight = 2;
                    else weight = 1;

                    int current_rank = visits * weight;

                    if (current_rank > highest_rank) {
                        highest_rank = current_rank;
                        strcpy(best_match, path);
                        found = 1;
                    }
                }
            }
        }
    }
    fclose(f);
    return found;
}

/*
 * 'hop' builtin: changes directory with support for absolute/relative paths,
 * '~' home expansion, '-' previous directory tracking, and frecency fuzzy jump fallback.
 */
int execute_hop(char** args, int arg_count, char *home_directory, char *prev_directory) {
    if (arg_count == 1) {
        args[1] = "~";
        arg_count = 2;
    }

    for (int i = 1; i < arg_count; i++) {
        char curr_directory[4096];
        getcwd(curr_directory, sizeof(curr_directory));
        char go_to_path[4096] = "";
        char* target_pathway = args[i];

        if (strcmp(target_pathway, "~") == 0) {
            strcpy(go_to_path, home_directory);
        } else if (strcmp(target_pathway, "-") == 0) {
            if (strlen(prev_directory) == 0) {
                printf("cshell: hop: no previous directory\n");
                return -1;
            }
            strcpy(go_to_path, prev_directory);
        } else if (target_pathway[0] == '~') {
            snprintf(go_to_path, sizeof(go_to_path), "%s%s", home_directory, target_pathway + 1);
        } else {
            strcpy(go_to_path, target_pathway);
        }

        if (chdir(go_to_path) != 0) {
            char best_match[4096];
            if (resolve_frecency(target_pathway, home_directory, best_match)) {
                chdir(best_match);
                strcpy(go_to_path, best_match);
            } else {
                printf("hop: no such directory\n");
                return -1;
            }
        }

        strcpy(prev_directory, curr_directory);

        char new_directory[4096];
        getcwd(new_directory, sizeof(new_directory));
        printf("%s\n", new_directory);

        if (strcmp(target_pathway, ".") != 0) {
            update_frecency(new_directory, home_directory);
        }
    }
    return 0;
}