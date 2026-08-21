#ifndef EXECUTE_H
#define EXECUTE_H

#include "lexer.h"

void execute_pipeline(token* token_list, char* home_directory, char* prev_directory);

#endif