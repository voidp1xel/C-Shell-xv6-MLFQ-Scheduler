#ifndef EXECUTE_H
#define EXECUTE_H

#include "lexer.h"

void execute_pipeline(token* start_token, token* end_token, int is_background, char* home_directory, char* prev_directory);

#endif