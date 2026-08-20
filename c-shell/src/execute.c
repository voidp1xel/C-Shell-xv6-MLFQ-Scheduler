#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h> 
#include <fcntl.h>
#include <sys/stat.h>
#include "../include/execute.h"

int resolve_command_path(const char* command, char* resolved_path) {
    char cwd[4096];
    char path_buffer[4096];

    if(strchr(command, '/') != NULL){ //returns pointer to first instance of character in string. eg /bin/ls
        if(access(command, X_OK) == 0){ //if the user types ./build.sh or /bin/ls, strchr checks for /. if it exists we test its permissions
            strcpy(resolved_path, command);//resolved path us the empty buffer where we will store the final absolute path if we find it
            return 1; //success
        }
        return 0;
    }

    int skip_cwd = 0;
    const char* actual_command = command;
    
    if(command[0] == '%'){ //if we see a %, it skips pointer one forwad. eg %ls. we check only ls and skip %
        skip_cwd = 1;
        actual_command = command + 1;
    }

    if(!skip_cwd && getcwd(cwd, sizeof(cwd)) != NULL){ //if not conatin %, we stitch cwd and command and check for permissions
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", cwd, actual_command);
        if(access(path_buffer, X_OK) == 0){
            strcpy(resolved_path, path_buffer);
            return 1;
        }
    }

    char* path_env = getenv("PATH"); //asks os for value of path variable.  The OS returns a pointer to a massive string that looks like this: /usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin. It is a list of directories separated by colons (:).
    if(path_env != NULL){
        char* path_copy = strdup(path_env); //copy required bcs strtok modifies the original string
        char* dir = strtok(path_copy, ":"); //dir now points to just the first directory, for example: /usr/local/bin

        while(dir != NULL){
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir, actual_command); //stitches commands eg /usr/bin/ls
            
            if(access(path_buffer, X_OK) == 0){ //checks if it is executable
                strcpy(resolved_path, path_buffer);
                free(path_copy);
                return 1;
            }
            dir = strtok(NULL, ":"); //dont start over, iterate in the same string
        }
        free(path_copy);
    }

    return 0;
}

void execute_pipeline(token* token_list) {
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
                printf("cshell: invalid syntax\n"); //trailing pipe like "ls |" or "ls | | cat"
                return;
            }
        }
        current = current->next;
    }

    int prev_pipe_fd = -1; //holds the read end of the previous pipe so the next command can read from it
    pid_t pids[256]; //array to store all child process IDs so we can wait for them all at the end

    //phase 2: loop through every command in the pipeline one by one
    for(int i = 0; i < cmd_count; i++){
        
        //1. parse the specific arguments and redirections for THIS pipeline stage
        char* args[256]; 
        int arg_count = 0;
        char* inputs[256]; 
        int input_count = 0;
        char* outputs[256]; 
        int output_append[256]; 
        int output_count = 0;
        int syntax_error = 0;

        token* curr = pipeline_cmds[i];
        while(curr != NULL && curr->type != pipe_type){ //stop parsing when we hit the next pipe
            if(curr->type == word_type){
                if(arg_count < 255) args[arg_count++] = curr->value;
            }
            else if(curr->type == less_than_type){
                curr = curr->next;
                if(curr && curr->type == word_type) inputs[input_count++] = curr->value;
                else{ 
                    syntax_error = 1;
                    break; 
                }
            } 
            else if(curr->type == greater_than_type || curr->type == greater_than_greater_than_type){
                int is_app = (curr->type == greater_than_greater_than_type);
                curr = curr->next;
                if(curr && curr->type == word_type){
                    outputs[output_count] = curr->value;
                    output_append[output_count++] = is_app;
                } 
                else{ 
                    syntax_error = 1; 
                    break; 
                }
            }
            curr = curr->next;
        }
        args[arg_count] = NULL; //crucial NULL termination for execv

        if (syntax_error) {
            printf("cshell: invalid syntax\n");
            if (prev_pipe_fd != -1) close(prev_pipe_fd);
            return;
        }

        //2. resolve path for this specific command
        char resolved_path[4096];
        int path_found = resolve_command_path(args[0], resolved_path);

        //3. create the OS pipe (but not for the very last command, as it prints to screen or file)
        int pipe_fd[2]; //pipe_fd[0] for the read end, pipe_fd[1] for write end
        if (i < cmd_count - 1) {
            if (pipe(pipe_fd) < 0) { 
                perror("cshell"); 
                return; 
            }
        }

        //4. fork the child process for this command
        pid_t pid = fork(); //needed to hold id of running process
        
        if(pid < 0){
            perror("cshell");
            return;
        }
        
        else if(pid == 0){ //inside child fork as pid = 0
            
            //wire the pipes using dup2!
            if (prev_pipe_fd != -1) { //if there is a pipe behind us, read from it instead of keyboard
                dup2(prev_pipe_fd, STDIN_FILENO);
                close(prev_pipe_fd);
            }
            if (i < cmd_count - 1) { //if there is a pipe ahead of us, write to it instead of screen
                dup2(pipe_fd[1], STDOUT_FILENO);
                close(pipe_fd[0]); //child doesnt need to read from its own pipe
                close(pipe_fd[1]);
            }

            //spec requirement: if command missing, print error but let rest of pipeline run (don't crash shell)
            if (!path_found) {
                printf("cshell: command not found (%s)\n", args[0]);
                exit(EXIT_FAILURE); //exiting here safely closes the pipe so the next command gets EOF
            }

            if(args[0][0] == '%'){ //if user types %ls or sum like that, we move pointer forward to make it think user type ls
                args[0] = args[0] + 1;
            }

            //multiplexer phase 1: input combination (overrides the pipe if user provides '<')
            if(input_count > 0){
                FILE* in_tmp = tmpfile(); //create hidden file to hold all combined inputs
                for(int k = 0; k < input_count; k++){ //loop through every input file provided by user eg a.txt, b.txt. open them in read only mode
                    int fd = open(inputs[k], O_RDONLY);
                    if(fd < 0){
                        printf("cshell: no such file or directory\n"); //spec requirement if input file is missing
                        exit(EXIT_FAILURE);
                    }
                    char buf[4096]; //reading file in 4096 byte chunks and dump in in_tmp
                    int bytes;
                    while((bytes = read(fd, buf, sizeof(buf))) > 0){
                        fwrite(buf, 1, bytes, in_tmp); //write contents of user file into temp file
                    }
                    close(fd);
                }
                fflush(in_tmp); //force os to write ram data to disk
                rewind(in_tmp); //bring pointer back to start of temp file so command can read it
                dup2(fileno(in_tmp), STDIN_FILENO); //dup connects one std input to file at a time, overriding pipe
            }

            //multiplexer phase 2 & 3: output multiplexer (overrides the pipe if user provides '>')
            if(output_count > 0){
                //we check permissions now so we can abort before forking if user doesnt have write access
                for(int k = 0; k < output_count; k++){
                    int flags = O_WRONLY | O_CREAT; //flags are binary in c
                    if(output_append[k]) flags |= O_APPEND; //bitwise OR operator to add append flag. output_append[i]=1 only if >> found
                    else flags |= O_TRUNC; //trunc flag deletes all existing text in file
                    
                    int test_fd = open(outputs[k], flags, 0644); //0644 gives rw-r--r-- permissions
                    if(test_fd < 0){
                        printf("cshell: unable to create file for writing\n"); //spec requirement
                        exit(EXIT_FAILURE);
                    }
                    close(test_fd); //close it immediately, we will reopen it later to write actual data
                }

                FILE* out_tmp = tmpfile(); //hidden file to capture the commands output
                int out_fd = fileno(out_tmp);

                //THE GRANDCHILD TRICK: To multiplex output while supporting pipes, we fork again!
                pid_t gc_pid = fork();
                if (gc_pid == 0) {
                    //inside grandchild fork: this process just redirects its output to out_tmp and runs the program
                    dup2(out_fd, STDOUT_FILENO);
                    execv(resolved_path, args); //exec command called. if it works, grandchild process is replaced by new data
                    perror("cshell"); 
                    exit(EXIT_FAILURE);
                } 
                else {
                    //inside child fork (parent of grandchild): waits for grandchild to finish, then copies the output
                    int st; 
                    waitpid(gc_pid, &st, 0);

                    fflush(out_tmp);
                    rewind(out_tmp); //rewind our capture file to the beginning

                    int dest_fds[256];
                    for(int k = 0; k < output_count; k++){ //open all destination files at once
                        int flags = O_WRONLY | O_CREAT;
                        if(output_append[k]) flags |= O_APPEND;
                        else flags |= O_TRUNC;
                        dest_fds[k] = open(outputs[k], flags, 0644); 
                    }

                    char buf[4096];
                    int bytes;
                    while((bytes = fread(buf, 1, sizeof(buf), out_tmp)) > 0){ //read chunk from capture file(trapped output 4096 sized chunks at a time)
                        for(int k = 0; k < output_count; k++){ //loop to write exact same chunk to every file user requested
                            if(dest_fds[k] >= 0){
                                write(dest_fds[k], buf, bytes); //write exact chunk to every user requested file
                            }
                        }
                    }

                    for(int k = 0; k < output_count; k++){
                        if(dest_fds[k] >= 0) close(dest_fds[k]); //close all target files
                    }
                    
                    exit(EXIT_SUCCESS); //child dies happily after distributing the output
                }
            } 
            else {
                //no output redirection requested, execute normally (output goes into pipe or terminal screen)
                execv(resolved_path, args);
                perror("cshell"); //since child did not get replaced by new data, we knew the exec command failed
                exit(EXIT_FAILURE);
            }
        } 
        else {
            //inside parent (C-Shell): clean up pipes and save pid
            pids[i] = pid; //save child id so we can wait for all of them at the end
            
            if (prev_pipe_fd != -1) close(prev_pipe_fd); //close the old read end so we don't leak memory
            
            if (i < cmd_count - 1) {
                close(pipe_fd[1]); //parent must close the write end of the pipe to prevent deadlocks!
                prev_pipe_fd = pipe_fd[0]; //save the new read end for the next iteration of the loop
            }
        }
    }

    //phase 3: wait for all commands in the pipeline to finish simultaneously
    for (int i = 0; i < cmd_count; i++) {
        int status;
        waitpid(pids[i], &status, 0); //wait until process with id = pids[i] terminates
    }
}