#include <stdio.h>
#include "../include/parser.h"
#include "../include/execute.h"

void parse_and_execute(token* token_list, char* home_directory, char* prev_directory){
    if (token_list == NULL) return; //failsafe

    token* curr = token_list;
    token* start = curr; 

    while (curr != NULL) {
        if (curr->type == semicolon_type || curr->type == ambersand_type) { 
            int is_bg = (curr->type == ambersand_type); 
            
            int status = execute_pipeline(start, curr, is_bg, home_directory, prev_directory);
            
            // D1 Sequence Abortion Rule: If a command fails to start, abort the remaining sequence
            if (status == -1 && curr->type == semicolon_type) {
                return; 
            }
            
            start = curr->next; 
        }
        curr = curr->next;
    }
    
    if (start != NULL) {
        execute_pipeline(start, NULL, 0, home_directory, prev_directory);
    }
}