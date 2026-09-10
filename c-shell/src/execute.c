#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h> 
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include "../include/execute.h"
#include "../include/hop.h"
#include "../include/reveal.h"
#include "../include/peek.h"
#include "../include/locate.h"
#include "../include/jobs.h" 

int resolve_command_path(const char* command, char* resolved_path) {
    char cwd[4096];
    char path_buffer[4096];

    if(strchr(command, '/') != NULL){ 
        if(access(command, X_OK) == 0){ 
            strcpy(resolved_path, command);
            return 1; 
        }
        return 0;
    }

    int skip_cwd = 0;
    const char* actual_command = command;
    
    if(command[0] == '%'){ 
        skip_cwd = 1;
        actual_command = command + 1;
    }

    if(!skip_cwd && getcwd(cwd, sizeof(cwd)) != NULL){ 
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", cwd, actual_command);
        if(access(path_buffer, X_OK) == 0){
            strcpy(resolved_path, path_buffer);
            return 1;
        }
    }

    char* path_env = getenv("PATH"); 
    if(path_env != NULL){
        char* path_copy = strdup(path_env); 
        char* dir = strtok(path_copy, ":"); 

        while(dir != NULL){
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir, actual_command); 
            
            if(access(path_buffer, X_OK) == 0){ 
                strcpy(resolved_path, path_buffer);
                free(path_copy);
                return 1;
            }
            dir = strtok(NULL, ":"); 
        }
        free(path_copy);
    }
    return 0;
}

void execute_pipeline(token* start_token, token* end_token, int is_background, char* home_directory, char* prev_directory) {
    if (start_token == end_token || start_token == NULL) return; //empty command failsafe

    token* pipeline_cmds[256]; 
    int cmd_count = 0;
    
    token* current = start_token;
    pipeline_cmds[cmd_count++] = current;

    //phase 1: scan the line and split it at every pipe (|), stop if we hit end_token (like ; or &)
    while(current != end_token && current != NULL){
        if(current->type == pipe_type){
            if(current->next != end_token && current->next != NULL && current->next->type == word_type){
                pipeline_cmds[cmd_count++] = current->next; 
            }else{
                printf("cshell: invalid syntax\n"); 
                return;
            }
        }
        current = current->next;
    }

    int prev_pipe_fd = -1; 
    pid_t pids[256]; 
    pid_t pgid = 0; //process group id. all commands in this pipeline will share this ID
    char full_cmd_str[1024] = ""; //string to hold the full command for the activities tracker

    //phase 2: loop through every command in the pipeline one by one
    for(int i = 0; i < cmd_count; i++){
        
        //1. parse the specific arguments and redirections for THIS pipeline stage
        char* args[256]; int arg_count = 0;
        char* inputs[256]; int input_count = 0;
        char* outputs[256]; int output_append[256]; int output_count = 0;
        int syntax_error = 0;

        token* curr = pipeline_cmds[i];
        while(curr != end_token && curr != NULL && curr->type != pipe_type){ 
            if(curr->type == word_type){
                if(arg_count < 255) {
                    args[arg_count++] = curr->value;
                    strcat(full_cmd_str, curr->value); //build the string for activities
                    strcat(full_cmd_str, " ");
                }
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
        args[arg_count] = NULL; 
        if (i < cmd_count - 1) strcat(full_cmd_str, "| "); 

        if (syntax_error) {
            printf("cshell: invalid syntax\n");
            if (prev_pipe_fd != -1) close(prev_pipe_fd);
            return;
        }

        //2. Built-in Intrinsic Detection
        int is_builtin = 0;
        if (args[0] && (strcmp(args[0], "hop") == 0 || strcmp(args[0], "reveal") == 0 ||
                        strcmp(args[0], "peek") == 0 || strcmp(args[0], "locate") == 0 ||
                        strcmp(args[0], "activities") == 0 || strcmp(args[0], "ping") == 0 ||
                        strcmp(args[0], "resume") == 0)) {
            is_builtin = 1;
        }

        //Special Case: Standalone state-modifying intrinsics MUST execute in the parent shell and RETURN immediately!
        if (is_builtin && cmd_count == 1) {
            if (strcmp(args[0], "hop") == 0) {
                execute_hop(args, arg_count, home_directory, prev_directory);
                return; //exit pipeline immediately so we don't trigger terminal control and freeze!
            }
            if (strcmp(args[0], "activities") == 0) {
                execute_activities();
                return;
            }
            if (strcmp(args[0], "ping") == 0) {
                execute_ping(args, arg_count);
                return;
            }
            if (strcmp(args[0], "resume") == 0) {
                execute_resume(args, arg_count);
                return;
            }
        }

        char resolved_path[4096];
        int path_found = 0;
        if (!is_builtin) path_found = resolve_command_path(args[0], resolved_path);

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
            
            //child processes should listen to ctrl-c and ctrl-z, so we reset signals to default!
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);

            //grouping: every pipeline gets its own process group to manage signals easily
            if (pgid == 0) pgid = getpid(); //if this is the first child, it becomes the group leader
            setpgid(getpid(), pgid);

            //if it's a foreground job, we hand over control of the terminal to this process group
            if (!is_background) {
                tcsetpgrp(STDIN_FILENO, pgid);
            }

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

            if (!is_builtin && !path_found) {
                printf("cshell: command not found (%s)\n", args[0]);
                exit(EXIT_FAILURE); 
            }

            if(!is_builtin && args[0][0] == '%') args[0] = args[0] + 1; 

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
                dup2(fileno(in_tmp), STDIN_FILENO); 
            }

            //multiplexer phase 2 & 3: output multiplexer 
            if(output_count > 0){
                for(int k = 0; k < output_count; k++){
                    int flags = O_WRONLY | O_CREAT | (output_append[k] ? O_APPEND : O_TRUNC); 
                    int test_fd = open(outputs[k], flags, 0644); 
                    if(test_fd < 0){
                        printf("cshell: unable to create file for writing\n"); 
                        exit(EXIT_FAILURE);
                    }
                    close(test_fd); 
                }

                FILE* out_tmp = tmpfile(); 
                int out_fd = fileno(out_tmp);

                pid_t gc_pid = fork();
                if (gc_pid == 0) {
                    dup2(out_fd, STDOUT_FILENO); 
                    
                    if (is_builtin) {
                        if (strcmp(args[0], "reveal") == 0) execute_reveal(args, arg_count, home_directory, prev_directory);
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
                else {
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
                    exit(EXIT_SUCCESS); 
                }
            } 
            else {
                if (is_builtin) {
                    if (strcmp(args[0], "reveal") == 0) execute_reveal(args, arg_count, home_directory, prev_directory);
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
            
            if (pgid == 0) pgid = pid; 
            setpgid(pid, pgid); 

            if (prev_pipe_fd != -1) close(prev_pipe_fd); 
            if (i < cmd_count - 1) {
                close(pipe_fd[1]); 
                prev_pipe_fd = pipe_fd[0]; 
            }
        }
    }

    //phase 3: execution wait state
    if (is_background) {
        int jid = add_job(pgid, pgid, full_cmd_str);
        printf("[%d] %d\n", jid, pgid);
    } 
    else {
        tcsetpgrp(STDIN_FILENO, pgid); //give terminal to foreground job before waiting

        for (int i = 0; i < cmd_count; i++) {
            if (pids[i] > 0) { 
                int status;
                waitpid(pids[i], &status, WUNTRACED); //wait for them to finish or stop
                
                if (WIFSTOPPED(status)) { //user hit ctrl-z
                    int jid = add_job(pgid, pgid, full_cmd_str);
                    mark_job_stopped(pgid);
                    printf("\n[%d] + Stopped %s\n", jid, full_cmd_str);
                    break; 
                }
            }
        }

        tcsetpgrp(STDIN_FILENO, getpgrp()); //reclaim terminal control using getpgrp() safely!
    }
}