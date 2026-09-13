// Include standard I/O for debugging/printing if needed.
#include <stdio.h>
// Include our custom header files to access the token definitions and execution logic.
#include "../include/parser.h"
#include "../include/execute.h"

// This function takes the raw linked list of tokens and splits it into discrete command chunks 
// based on semicolons (;) or ampersands (&), then sends them off to be executed.
void parse_and_execute(token* token_list, char* home_directory, char* prev_directory){
    
    // Failsafe: if the lexer returned nothing (e.g., empty input), just return to the main loop.
    if (token_list == NULL) return; 

    // We use two pointers to traverse the linked list. 
    // 'curr' moves forward to find operators, 'start' marks the beginning of the current command chunk.
    token* curr = token_list;
    token* start = curr; 

    while (curr != NULL) {
        
        // If we hit a command separator: a semicolon (sequential) or an ampersand (background).
        if (curr->type == semicolon_type || curr->type == ambersand_type) { 
            
            // Flag to determine if this specific chunk should run in the background. 
            // Evaluates to 1 (true) if it's an ampersand, 0 (false) if semicolon.
            int is_bg = (curr->type == ambersand_type); 
            
            // Pass the chunk (from 'start' up to 'curr') to the executor. 
            // execute_pipeline will handle built-ins, system commands, and piping.
            // It returns a status code (e.g., -1 means the command completely failed to launch).
            int status = execute_pipeline(start, curr, is_bg, home_directory, prev_directory);
            
            // --- D1 Sequence Abortion Rule ---
            // The rubric states that if a command in a sequential list completely fails to execute 
            // (meaning the binary wasn't found, returning -1), we must stop executing the REST of the sequence.
            // Note: A command exiting with an error code (like `ls` on a fake file) is NOT a failure to launch,
            // so we only abort if status == -1 and we are processing a semicolon sequence.
            if (status == -1 && curr->type == semicolon_type) {
                return; // Immediately exit the parser, ignoring any tokens left in the list.
            }
            
            // Move the 'start' pointer to the token right after the separator we just processed,
            // so the next loop iteration starts parsing the next command chunk.
            start = curr->next; 
        }
        
        // Move to the next token in the linked list.
        curr = curr->next;
    }
    
    // --- Final Dangling Command ---
    // If the loop finishes but 'start' isn't NULL, it means the user typed a command that 
    // didn't end with a ';' or '&' (e.g., "echo hello"). 
    // We execute that final chunk here as a standard, foreground process (is_bg = 0).
    if (start != NULL) {
        execute_pipeline(start, NULL, 0, home_directory, prev_directory);
    }
}