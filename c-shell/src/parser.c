#include <stdio.h>
#include <string.h>
#include "../include/parser.h"
#include "../include/hop.h"
#include "../include/reveal.h"
#include "../include/peek.h"
#include "../include/locate.h"
#include "../include/execute.h"

void parse_and_execute(token* token_list, char* home_directory, char* prev_directory){
    if (token_list == NULL) return; //failsafe

    //PARSER FOR HOP
    if(token_list->type == word_type && strcmp(token_list->value, "hop") == 0){
        token* current_arg = token_list->next;
        if (current_arg == NULL) { //just written hop in command. redirects as ~
            execute_hop(NULL, home_directory, prev_directory);
        }
        else{ //chained commands
            while (current_arg != NULL && current_arg->type == word_type){
                execute_hop(current_arg->value, home_directory, prev_directory);
                current_arg = current_arg->next;
            }
        }
    }

    //PARSER FOR REVEAL
    else if(token_list->type == word_type && strcmp(token_list->value, "reveal") == 0){
        int a_flag = 0;
        int t_flag = 0;
        char* target = NULL;
        int invalid_syntax = 0;

        token* current_arg = token_list->next;

        while (current_arg != NULL && current_arg->type == word_type){
            if (current_arg->value[0] == '-' && strlen(current_arg->value) > 1){ // if user types something like reveal -att
                for (int i = 1; current_arg->value[i] != '\0'; i++) { //checks syntax and keeps flags
                    if (current_arg->value[i] == 'a') a_flag = 1;
                    else if (current_arg->value[i] == 't') t_flag = 1;
                    else invalid_syntax = 1;
                }
            }
            else{ //if reveal command didnt start with '-' or if it was exactly reveal -. this implies it must talk about src or ~
                if (target != NULL) {
                    invalid_syntax = 1; //if target is already filled with previous command. reveal cant take 2 targets. eg reveal src include.
                }
                else {
                    target = current_arg->value; //eg reveal include
                }
            }
            current_arg = current_arg->next;
        }
        if (invalid_syntax) {
            printf("reveal: invalid syntax\n");
        }
        else {
            execute_reveal(a_flag, t_flag, target, home_directory, prev_directory);
        }
    }

    //PARSER FOR PEEK
    else if(token_list->type == word_type && strcmp(token_list->value, "peek") == 0){
        int n_flag = 0;
        int r_flag = 0;
        char* targets[256]; //array to hold 256 file names
        int target_count = 0;
        int invalid_syntax = 0;

        token* current_arg = token_list->next;

        while (current_arg != NULL && current_arg->type == word_type){
            //check for if theres a flag '-' and not just a standalone '-'
            if (current_arg->value[0] == '-' && strlen(current_arg->value) > 1){ 
                for (int i = 1; current_arg->value[i] != '\0'; i++) { 
                    if (current_arg->value[i] == 'n') n_flag = 1;
                    else if (current_arg->value[i] == 'r') r_flag = 1;
                    else invalid_syntax = 1; //unrecognized flag
                }
            }
            //its a filename or a standalone '-'
            else{
                if (target_count < 256) {
                    targets[target_count] = current_arg->value;
                    target_count++;
                }
            }
            current_arg = current_arg->next;
        }
        if (invalid_syntax) {
            printf("peek: invalid syntax\n");
        }
        else{
            execute_peek(n_flag, r_flag, targets, target_count);
        }
    }

    //PARSER FOR LOCATE
    else if(token_list->type == word_type && strcmp(token_list->value, "locate") == 0){
        char* targets[256]; //array to hold command names
        int target_count = 0;

        token* current_arg = token_list->next;

        while (current_arg != NULL && current_arg->type == word_type){
            if (target_count < 256) {
                targets[target_count] = current_arg->value;
                target_count++;
            }
            current_arg = current_arg->next;
        }

        if (target_count == 0) {
            printf("locate: invalid syntax\n"); //spec explicitly requires this if no args given
        }
        else{
            execute_locate(targets, target_count);
        }
    }

    // CATCH-ALL FOR EXTERNAL COMMANDS & REDIRECTION (Part C1, C2, C3)
    else if (token_list->type == word_type) {
        char* args[256];
        int arg_count = 0;
        
        char* inputs[256];
        int input_count = 0;
        
        char* outputs[256];
        int output_append[256]; // 1 for >> (append), 0 for > (truncate)
        int output_count = 0;
        
        int syntax_error = 0;

        token* current_arg = token_list;

        // Loop through tokens. Stop if we hit a pipe or background operator
        while (current_arg != NULL && current_arg->type != semicolon_type && 
               current_arg->type != ambersand_type && current_arg->type != pipe_type) {
            
            if (current_arg->type == word_type) {
                if (arg_count < 255) {
                    args[arg_count++] = current_arg->value;
                }
            } 
            else if (current_arg->type == less_than_type) { // Found a '<'
                current_arg = current_arg->next;
                if (current_arg != NULL && current_arg->type == word_type) {
                    inputs[input_count++] = current_arg->value;
                } else {
                    syntax_error = 1; break; // Missing filename after <
                }
            } 
            else if (current_arg->type == greater_than_type || current_arg->type == greater_than_greater_than_type) { // Found a '>' or '>>'
                int is_append = (current_arg->type == greater_than_greater_than_type);
                current_arg = current_arg->next;
                if (current_arg != NULL && current_arg->type == word_type) {
                    outputs[output_count] = current_arg->value;
                    output_append[output_count] = is_append; // Mark 1 if append, 0 if truncate
                    output_count++;
                } else {
                    syntax_error = 1; break; // Missing filename after > or >>
                }
            }
            current_arg = current_arg->next;
        }
        args[arg_count] = NULL; // Crucial NULL termination for execv

        if (syntax_error) {
            printf("cshell: invalid syntax\n");
        } else {
            // Pass the extracted arrays down to the executor
            execute_external_command(args, inputs, input_count, outputs, output_append, output_count);
        }
    }
}