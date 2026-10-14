#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/lexer.h"

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

token* create(token_type type, char* value) {
    token* newtoken = malloc(sizeof(token));
    newtoken->next = NULL;
    newtoken->type = type;
    if (value == NULL) {
        newtoken->value = NULL;
    } else {
        newtoken->value = strdup(value);
    }
    return newtoken;
}

void add(token** head, token** tail, token_type type, char* val) {
    token* newtoken = create(type, val);
    if (*head == NULL) {
        *head = newtoken;
        *tail = newtoken;
    } else {
        (*tail)->next = newtoken;
        *tail = newtoken;
    }
}

/*
 * Tokenizes raw shell input into a linked list of tokens using a Finite State Machine (FSM).
 * Handles whitespace delimitation, single/double quotes, escape sequences, and shell operators.
 */
token* make_token(char* user_input) {
    token* head = NULL;
    token* tail = NULL;

    state curr_state = normal_state;
    char buffer[4096];
    int buffer_index = 0;

    for (int i = 0; user_input[i] != '\0'; i++) {
        char c = user_input[i];

        /* State: Normal (unquoted) */
        if (curr_state == normal_state) {
            if (c == '\t' || c == ' ' || c == '\r' || c == '\n') {
                if (buffer_index > 0) {
                    buffer[buffer_index] = '\0';
                    add(&head, &tail, word_type, buffer);
                    buffer_index = 0;
                }
            } else if (c == '"') {
                curr_state = double_quote;
            } else if (c == '\'') {
                curr_state = single_quote;
            } else if (c == '\\') {
                if (user_input[i + 1] == '\0') {
                    printf("cshell: invalid syntax\n");
                    clear_tokens(head);
                    return NULL;
                } else {
                    if (buffer_index < 4095) {
                        buffer[buffer_index++] = user_input[i + 1];
                    } else {
                        printf("cshell: word too long\n");
                        clear_tokens(head);
                        return NULL;
                    }
                    i++;
                }
            } else if (c == ';' || c == '&' || c == '|' || c == '<' || c == '>') {
                if (buffer_index > 0) {
                    buffer[buffer_index] = '\0';
                    add(&head, &tail, word_type, buffer);
                    buffer_index = 0;
                }

                if (c == ';') {
                    add(&head, &tail, semicolon_type, NULL);
                } else if (c == '&') {
                    add(&head, &tail, ambersand_type, NULL);
                } else if (c == '|') {
                    add(&head, &tail, pipe_type, NULL);
                } else if (c == '<') {
                    add(&head, &tail, less_than_type, NULL);
                } else if (c == '>') {
                    if (user_input[i + 1] == '>') {
                        add(&head, &tail, greater_than_greater_than_type, NULL);
                        i++;
                    } else {
                        add(&head, &tail, greater_than_type, NULL);
                    }
                }
            } else {
                buffer[buffer_index++] = c;
            }
        }
        /* State: Single Quote (literal preservation) */
        else if (curr_state == single_quote) {
            if (c == '\'') {
                curr_state = normal_state;
            } else {
                buffer[buffer_index++] = c;
            }
        }
        /* State: Double Quote (preserves whitespace, permits escaping) */
        else if (curr_state == double_quote) {
            if (c == '"') {
                curr_state = normal_state;
            } else if (c == '\\') {
                if (user_input[i + 1] == '"' || user_input[i + 1] == '\\') {
                    if (buffer_index < 4095) {
                        buffer[buffer_index++] = user_input[i + 1];
                    } else {
                        printf("cshell: word too long\n");
                        clear_tokens(head);
                        return NULL;
                    }
                    i++;
                } else if (user_input[i + 1] != '\0') {
                    if (buffer_index < 4095) buffer[buffer_index++] = c;
                }
            } else {
                buffer[buffer_index++] = c;
            }
        }
    }

    /* Unclosed quote check */
    if (curr_state != normal_state) {
        printf("cshell: invalid syntax\n");
        clear_tokens(head);
        return NULL;
    }

    if (buffer_index > 0) {
        buffer[buffer_index] = '\0';
        add(&head, &tail, word_type, buffer);
    }

    return head;
}