#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

void parse_and_execute(token* token_list, char* home_directory, char* prev_directory);

#endif