#ifndef LEXER_H
#define LEXER_H

typedef enum{
    word_type, //cat, grep etc
    pipe_type, //|
    ambersand_type, //&
    semicolon_type, //;
    less_than_type, //<
    greater_than_type,//>
    greater_than_greater_than_type  //>>
}token_type;

typedef enum{
    normal_state,
    single_quote,
    double_quote
}state; //fsm state

typedef struct token{
    token_type type;
    char* value;
    struct token* next;
}token;

#endif