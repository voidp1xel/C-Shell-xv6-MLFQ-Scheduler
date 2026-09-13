#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/lexer.h"

// Helper function: Destroys the linked list of tokens in memory to prevent leaks[cite: 7].
void clear_tokens(token* head) {
    token* current = head;
    while (current != NULL) {
        token* next = current->next;
        if (current->value != NULL) {
            free(current->value);
        }
        free(current);
        current = next;
    }
}

// Helper function: Allocates memory to create a new token node[cite: 7].
token* create(token_type type, char* value){
    token* newtoken = malloc(sizeof(token));
    newtoken->next = NULL;
    newtoken->type = type;
    if(value == NULL){
        newtoken -> value = NULL;
    }
    else{
        newtoken->value = strdup(value); // Duplicate the string into the token[cite: 7].
    }
    return newtoken;
}

// Helper function: Appends a newly created token to the end of our linked list[cite: 7].
void add(token** head, token** tail, token_type type, char* val){ 
    token* newtoken = create(type, val);
    if(*head == NULL){      // If list is empty, this is the first item[cite: 7].
        *head = newtoken;
        *tail = newtoken;
    }
    else{                   // Otherwise, attach it to the tail[cite: 7].
        (*tail)->next = newtoken;
        *tail = newtoken;
    }
}

// Core Lexer Engine: The Finite State Machine (FSM)[cite: 7].
token* make_token(char* user_input){
    token* head = NULL;
    token* tail = NULL;
    
    // Our FSM starts in "Normal" mode (not inside any quotes)[cite: 7].
    state curr_state = normal_state;
    char buffer[4096];
    int buffer_index = 0;
    
    // Loop through every single character the user typed[cite: 7].
    for(int i = 0; user_input[i] != '\0'; i++){
        char c = user_input[i];
        
        // --- FSM STATE 1: NORMAL (Outside of Quotes) ---[cite: 7]
        if(curr_state == normal_state){
            // If we hit whitespace (space, tab, enter)[cite: 7].
            if (c == '\t' || c == ' ' || c == '\r' || c == '\n'){
                // If we have letters in our buffer (like 'c' 'a' 't'), it's time to save them as a completed WORD token[cite: 7].
                if(buffer_index > 0){
                    buffer[buffer_index] = '\0';
                    add(&head, &tail, word_type, buffer);
                    buffer_index = 0; // Reset buffer for the next word[cite: 7].
                }
            }
            // State Transition: If we see a quote, switch to quote mode[cite: 7].
            else if(c == '"') curr_state = double_quote;
            else if(c == '\'') curr_state = single_quote;
            
            // The Backslash '\' acts as an escape character[cite: 7].
            else if (c == '\\') { 
                if (user_input[i+1] == '\0') { // Error: Can't have a backslash at the very end of the line[cite: 7].
                    printf("cshell: invalid syntax\n");
                    clear_tokens(head);
                    return NULL;
                } 
                else {
                    // Grab whatever character comes immediately AFTER the backslash and force it into the buffer[cite: 7].
                    if (buffer_index < 4095) {
                        buffer[buffer_index++] = user_input[i+1];
                    }
                    else {
                        printf("cshell: word too long\n"); // Buffer overflow protection[cite: 7].
                        clear_tokens(head);
                        return NULL;
                    }
                    i++; // Skip the next character in the loop since we just processed it[cite: 7].
                }
            }
            // Shell Operators: These break words just like spaces do[cite: 7].
            else if (c == ';' || c == '&' || c == '|' || c == '<' || c == '>'){
                
                // Flush the buffer to save the previous word[cite: 7].
                if(buffer_index > 0){
                    buffer[buffer_index] = '\0';
                    add(&head, &tail, word_type, buffer);
                    buffer_index = 0;
                }
                
                // Add the specific operator token to the list[cite: 7].
                if(c == ';') add(&head, &tail, semicolon_type, NULL);
                else if(c == '&') add(&head, &tail, ambersand_type, NULL);
                else if(c == '|') add(&head, &tail, pipe_type, NULL);
                else if(c == '<') add(&head, &tail, less_than_type, NULL);
                else if(c == '>'){
                    // Special check: Is it '>>' (append) or just '>' (overwrite)?[cite: 7]
                    if(user_input[i+1] == '>'){
                        add(&head, &tail, greater_than_greater_than_type, NULL);
                        i++; // Skip the second '>'[cite: 7].
                    }
                    else add(&head, &tail, greater_than_type, NULL);
                }
            }
            else{
                // Standard character (like 'a', 'b', 'c'). Just add it to the buffer[cite: 7].
                buffer[buffer_index] = c;
                buffer_index++;
            }
        }
        
        // --- FSM STATE 2: SINGLE QUOTE ('...') ---[cite: 7]
        // Inside single quotes, everything is literal. No operators or backslashes work[cite: 7].
        else if(curr_state == single_quote){
            if(c == '\''){ 
                curr_state = normal_state; // Closing quote: transition back to normal mode[cite: 7].
            }
            else{
                buffer[buffer_index] = c; // Add literally every character to the buffer[cite: 7].
                buffer_index++;
            }
        }
        
        // --- FSM STATE 3: DOUBLE QUOTE ("...") ---[cite: 7]
        // Inside double quotes, operators don't work, but backslashes DO work for escaping[cite: 7].
        else if(curr_state == double_quote){
            if(c == '"'){
                curr_state = normal_state; // Closing quote[cite: 7].
            }
            else if (c == '\\') {
                // If we see \", we escape the quote so it doesn't close our state[cite: 7].
                if (user_input[i + 1] == '"' || user_input[i + 1] == '\\') {
                    if (buffer_index < 4095) {
                        buffer[buffer_index++] = user_input[i + 1];
                    }
                    else {
                        printf("cshell: word too long\n");
                        clear_tokens(head);
                        return NULL;
                    }
                    i++;
                } 
                else if (user_input[i + 1] != '\0') {
                    if (buffer_index < 4095) buffer[buffer_index++] = c;
                }
            }
            else{
                buffer[buffer_index] = c; // Standard character[cite: 7].
                buffer_index++;
            }
        }
    }
    
    // ERROR CHECK: If the loop finishes but we are still stuck inside a quote state, 
    // it means the user never typed a closing quote! (e.g., `echo "hello`)[cite: 7].
    if (curr_state != normal_state) { 
        printf("cshell: invalid syntax\n");
        clear_tokens(head);
        return NULL;
    }
    
    // Flush the final word from the buffer into a token if the user didn't press space before entering[cite: 7].
    if(buffer_index > 0){ 
        buffer[buffer_index] = '\0';
        add(&head, &tail, word_type, buffer);
    }

    return head; // Return the fully formed linked list to main.c[cite: 7]!
}