#include <stdio.h>
#include "../include/parser.h"
#include "../include/execute.h"

void parse_and_execute(token* token_list, char* home_directory, char* prev_directory){
    if (token_list == NULL) return; //failsafe

    //we moved all argument parsing directly into the execution engine! 
    //parser.c is now just a clean router that hands off the raw tokens to the pipeline manager.
    execute_pipeline(token_list, home_directory, prev_directory);
}