#include <stdio.h>
#include "../include/parser.h"
#include "../include/execute.h"

void parse_and_execute(token* token_list, char* home_directory, char* prev_directory){
    if (token_list == NULL) return; //failsafe

    token* curr = token_list;
    token* start = curr; //tracks start of current command sequence

    while (curr != NULL) {
        if (curr->type == semicolon_type || curr->type == ambersand_type) { //we found a boundary!
            int is_bg = (curr->type == ambersand_type); //boolean true if it was an &
            
            //hand this specific slice of tokens to the executor
            execute_pipeline(start, curr, is_bg, home_directory, prev_directory);
            
            start = curr->next; //move start pointer past the ; or &
        }
        curr = curr->next;
    }
    
    //execute the final block if the user didn't put a ; or & at the very end
    if (start != NULL) {
        execute_pipeline(start, NULL, 0, home_directory, prev_directory);
    }
}