#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h> 
#include <fcntl.h>
#include <sys/stat.h>
#include "../include/execute.h"
#include "../include/hop.h"
#include "../include/reveal.h"
#include "../include/peek.h"
#include "../include/locate.h"

int resolve_command_path(const char* command, char* resolved_path) {
    char cwd[4096];
    char path_buffer[4096];

    if(strchr(command, '/') != NULL){ //returns pointer to first instance of character in string
        if(access(command, X_OK) == 0){ //treat as literal path
            strcpy(resolved_path, command);
            return 1; //success
        }
        return 0;
    }

    int skip_cwd = 0;
    const char* actual_command = command;
    
    if(command[0] == '%'){ //skip cwd check if prefixed with %
        skip_cwd = 1;
        actual_command = command + 1;
    }

    if(!skip_cwd && getcwd(cwd, sizeof(cwd)) != NULL){ //stitch cwd and command
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", cwd, actual_command);
        if(access(path_buffer, X_OK) == 0){
            strcpy(resolved_path, path_buffer);
            return 1;
        }
    }

    char* path_env = getenv("PATH"); //search system path
    if(path_env != NULL){
        char* path_copy = strdup(path_env); //must copy bcs strtok modifies string
        char* dir = strtok(path_copy, ":"); 

        while(dir != NULL){
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir, actual_command); 
            
            if(access(path_buffer, X_OK) == 0){ //check executable permission
                strcpy(resolved_path, path_buffer);
                free(path_copy);
                return 1;
            }
            dir = strtok(NULL, ":"); //continue searching path
        }
        free(path_copy);
    }
    return 0;
}

void execute_pipeline(token* token_list, char* home_directory, char* prev_directory) {
    token* pipeline_cmds[256]; //array to hold the start token of each command separated by |
    int cmd_count = 0;
    
    token* current = token_list;
    pipeline_cmds[cmd_count++] = current;

    //phase 1: scan the line and split it at every pipe (|)
    while(current != NULL){
        if(current->type == pipe_type){
            if(current->next != NULL && current->next->type == word_type){
                pipeline_cmds[cmd_count++] = current->next; //save the start of the next command
            }else{
                printf("cshell: invalid syntax\n"); //trailing pipe error
                return;
            }
        }
        current = current->next;
    }

    int prev_pipe_fd = -1; //holds the read end of the previous pipe
    pid_t pids[256]; 

    //phase 2: loop through every command in the pipeline one by one
    for(int i = 0; i < cmd_count; i++){
        
        //1. parse the specific arguments and redirections for THIS pipeline stage
        char* args[256]; int arg_count = 0;
        char* inputs[256]; int input_count = 0;
        char* outputs[256]; int output_append[256]; int output_count = 0;
        int syntax_error = 0;

        token* curr = pipeline_cmds[i];
        while(curr != NULL && curr->type != pipe_type){ 
            if(curr->type == word_type){
                if(arg_count < 255) args[arg_count++] = curr->value;
            }
            else if(curr->type == less_than_type){
                curr = curr->next;
                if(curr && curr->type == word_type) inputs[input_count++] = curr->value;
                else{ syntax_error = 1; break; }
            } 
            else if(curr->type == greater_than_type || curr->type == greater_than_greater_than_type){
                int is_app = (curr->type == greater_than_greater_than_type);
                curr = curr->next;
                if(curr && curr->type == word_type){
                    outputs[output_count] = curr->value;
                    output_append[output_count++] = is_app;
                } 
                else{ syntax_error = 1; break; }
            }
            curr = curr->next;
        }
        args[arg_count] = NULL; //crucial NULL termination for execv

        if (syntax_error) {
            printf("cshell: invalid syntax\n");
            if (prev_pipe_fd != -1) close(prev_pipe_fd);
            return;
        }

        //2. Built-in Intrinsic Detection (Q&A 22 says they must work in pipes!)
        int is_builtin = 0;
        if (args[0] && (strcmp(args[0], "hop") == 0 || strcmp(args[0], "reveal") == 0 ||
                        strcmp(args[0], "peek") == 0 || strcmp(args[0], "locate") == 0)) {
            is_builtin = 1;
        }

        //Special Case: If hop is run standalone (no pipes), it MUST run in the parent shell to actually change the directory!
        if (is_builtin && strcmp(args[0], "hop") == 0 && cmd_count == 1) {
            execute_hop(args, arg_count, home_directory, prev_directory);
            continue; //skip the fork!
        }

        char resolved_path[4096];
        int path_found = 0;
        if (!is_builtin) {
            path_found = resolve_command_path(args[0], resolved_path); //only search hard drive for external commands
        }

        //3. create the OS pipe
        int pipe_fd[2]; 
        if (i < cmd_count - 1) {
            if (pipe(pipe_fd) < 0) { perror("cshell"); return; }
        }

        //4. fork the child process
        pid_t pid = fork(); 
        
        if(pid < 0){
            perror("cshell");
            return;
        }
        else if(pid == 0){ //inside child fork as pid = 0
            
            //wire the pipes using dup2!
            if (prev_pipe_fd != -1) { 
                dup2(prev_pipe_fd, STDIN_FILENO);
                close(prev_pipe_fd);
            }
            if (i < cmd_count - 1) { 
                dup2(pipe_fd[1], STDOUT_FILENO);
                close(pipe_fd[0]); 
                close(pipe_fd[1]);
            }

            //spec requirement: if command missing, print error but let rest of pipeline run
            if (!is_builtin && !path_found) {
                printf("cshell: command not found (%s)\n", args[0]);
                exit(EXIT_FAILURE); 
            }

            if(!is_builtin && args[0][0] == '%') args[0] = args[0] + 1; //strip % for actual command name

            //multiplexer phase 1: input combination 
            if(input_count > 0){
                FILE* in_tmp = tmpfile(); 
                for(int k = 0; k < input_count; k++){ 
                    int fd = open(inputs[k], O_RDONLY);
                    if(fd < 0){
                        printf("cshell: no such file or directory\n"); 
                        exit(EXIT_FAILURE);
                    }
                    char buf[4096]; 
                    int bytes;
                    while((bytes = read(fd, buf, sizeof(buf))) > 0) fwrite(buf, 1, bytes, in_tmp); 
                    close(fd);
                }
                fflush(in_tmp); 
                rewind(in_tmp); 
                dup2(fileno(in_tmp), STDIN_FILENO); //dup connects std input to file, overriding pipe
            }

            //multiplexer phase 2 & 3: output multiplexer 
            if(output_count > 0){
                //check permissions before forking grandchild
                for(int k = 0; k < output_count; k++){
                    int flags = O_WRONLY | O_CREAT | (output_append[k] ? O_APPEND : O_TRUNC); 
                    int test_fd = open(outputs[k], flags, 0644); 
                    if(test_fd < 0){
                        printf("cshell: unable to create file for writing\n"); 
                        exit(EXIT_FAILURE);
                    }
                    close(test_fd); 
                }

                FILE* out_tmp = tmpfile(); //hidden file to capture output
                int out_fd = fileno(out_tmp);

                //THE GRANDCHILD TRICK: fork again to trap output while supporting pipes!
                pid_t gc_pid = fork();
                if (gc_pid == 0) {
                    dup2(out_fd, STDOUT_FILENO); //redirect grandchild output to temp file
                    
                    if (is_builtin) {
                        if (strcmp(args[0], "hop") == 0) execute_hop(args, arg_count, home_directory, prev_directory);
                        else if (strcmp(args[0], "reveal") == 0) execute_reveal(args, arg_count, home_directory, prev_directory);
                        else if (strcmp(args[0], "peek") == 0) execute_peek(args, arg_count);
                        else if (strcmp(args[0], "locate") == 0) execute_locate(args, arg_count);
                        exit(EXIT_SUCCESS); //intrinsic must exit inside child!
                    } 
                    else {
                        execv(resolved_path, args); 
                        perror("cshell"); 
                        exit(EXIT_FAILURE);
                    }
                } 
                else {
                    //child waits for grandchild, then distributes the trapped output
                    int st; 
                    waitpid(gc_pid, &st, 0);

                    fflush(out_tmp);
                    rewind(out_tmp); 

                    int dest_fds[256];
                    for(int k = 0; k < output_count; k++){ 
                        int flags = O_WRONLY | O_CREAT | (output_append[k] ? O_APPEND : O_TRUNC);
                        dest_fds[k] = open(outputs[k], flags, 0644); 
                    }

                    char buf[4096];
                    int bytes;
                    while((bytes = fread(buf, 1, sizeof(buf), out_tmp)) > 0){ 
                        for(int k = 0; k < output_count; k++){ 
                            if(dest_fds[k] >= 0) write(dest_fds[k], buf, bytes); 
                        }
                    }

                    for(int k = 0; k < output_count; k++){
                        if(dest_fds[k] >= 0) close(dest_fds[k]); 
                    }
                    exit(EXIT_SUCCESS); //child dies happily after distributing
                }
            } 
            else {
                //no output redirection requested, output goes straight to pipe or terminal screen
                if (is_builtin) {
                    if (strcmp(args[0], "hop") == 0) execute_hop(args, arg_count, home_directory, prev_directory);
                    else if (strcmp(args[0], "reveal") == 0) execute_reveal(args, arg_count, home_directory, prev_directory);
                    else if (strcmp(args[0], "peek") == 0) execute_peek(args, arg_count);
                    else if (strcmp(args[0], "locate") == 0) execute_locate(args, arg_count);
                    exit(EXIT_SUCCESS); 
                } 
                else {
                    execv(resolved_path, args);
                    perror("cshell"); 
                    exit(EXIT_FAILURE);
                }
            }
        } 
        else {
            //inside parent (C-Shell): clean up pipes and save pid
            pids[i] = pid; 
            if (prev_pipe_fd != -1) close(prev_pipe_fd); 
            if (i < cmd_count - 1) {
                close(pipe_fd[1]); //prevent deadlocks!
                prev_pipe_fd = pipe_fd[0]; 
            }
        }
    }

    //phase 3: wait for all commands in the pipeline to finish simultaneously
    for (int i = 0; i < cmd_count; i++) {
        if (pids[i] > 0) { //only wait for valid pids (skip if it was a parent-run hop)
            int status;
            waitpid(pids[i], &status, 0); 
        }
    }
}