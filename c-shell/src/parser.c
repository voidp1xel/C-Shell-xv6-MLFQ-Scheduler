#include <stdio.h>

#include "../include/parser.h"
#include "../include/execute.h"

/*
 * Parses a stream of tokens into sequential (;) and background (&) command chains.
 * If a command in a sequential chain fails to launch (-1), subsequent commands
 * in that chain are aborted.
 */
void parse_and_execute(token* token_list, char* home_directory, char* prev_directory) {
    if (token_list == NULL) return;

    token* curr = token_list;
    token* start = curr;

    while (curr != NULL) {
        if (curr->type == semicolon_type || curr->type == ambersand_type) {
            int is_bg = (curr->type == ambersand_type);

            int status = execute_pipeline(start, curr, is_bg, home_directory, prev_directory);

            /* Abort subsequent sequential execution on command launch failure */
            if (status == -1 && curr->type == semicolon_type) {
                return;
            }

            start = curr->next;
        }
        curr = curr->next;
    }

    /* Execute trailing command without terminating delimiter */
    if (start != NULL) {
        execute_pipeline(start, NULL, 0, home_directory, prev_directory);
    }
}