#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// sys/wait.h is needed for waitpid(), which pauses the parent shell until the child command finishes.
#include <sys/wait.h> 
// fcntl.h provides file control options like O_RDONLY, O_WRONLY, O_CREAT for redirections.
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include "../include/execute.h"
#include "../include/hop.h"
#include "../include/reveal.h"
#include "../include/peek.h"
#include "../include/locate.h"
#include "../include/jobs.h" 
#include "../include/fun.h"

// This function checks if a command (like "ls" or "python") actually exists on the system.
// It searches the current directory and the system's PATH variable.
int resolve_command_path(const char* command, char* resolved_path) {
    char cwd[4096];
    char path_buffer[4096];

    // Check 1: Did the user type an exact path? (e.g., "./script.sh" or "/bin/ls")
    if(strchr(command, '/') != NULL){ 
        // access() with X_OK checks if the file exists AND we have permission to execute it.
        if(access(command, X_OK) == 0){ 
            strcpy(resolved_path, command); // It's valid, copy it to our output variable.
            return 1; // Success
        }
        return 0; // Fail
    }

    int skip_cwd = 0;
    const char* actual_command = command;
    
    // Check 2: The '%' prefix. The rubric says if a command starts with '%', we MUST skip 
    // the local directory check and only search the system PATH.
    if(command[0] == '%'){ 
        skip_cwd = 1;
        actual_command = command + 1; // Move the pointer forward 1 character to ignore the '%'.
    }

    // Check 3: Search the Current Working Directory (unless '%' was used).
    if(!skip_cwd && getcwd(cwd, sizeof(cwd)) != NULL){ 
        // Combine the current directory and command name into one string (e.g., "/home/user/my_program")
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", cwd, actual_command);
        if(access(path_buffer, X_OK) == 0){
            strcpy(resolved_path, path_buffer);
            return 1;
        }
    }

    // Check 4: Search the system PATH environment variable (e.g., "/usr/bin:/bin:/usr/sbin").
    char* path_env = getenv("PATH"); 
    if(path_env != NULL){
        // We duplicate the string because strtok modifies the string it works on.
        char* path_copy = strdup(path_env); 
        // strtok chops the string into pieces wherever it sees a colon (":").
        char* dir = strtok(path_copy, ":"); 

        while(dir != NULL){
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir, actual_command); 
            
            if(access(path_buffer, X_OK) == 0){ 
                strcpy(resolved_path, path_buffer);
                free(path_copy); // Prevent memory leaks
                return 1;
            }
            dir = strtok(NULL, ":"); // Get the next directory in the PATH
        }
        free(path_copy);
    }
    return 0; // The command does not exist anywhere.
}

// This function handles the complex logic of running commands, pipes (|), and redirections (<, >).
int execute_pipeline(token* start_token, token* end_token, int is_background, char* home_directory, char* prev_directory) {
    if (start_token == end_token || start_token == NULL) return 0; // Empty command failsafe

    token* pipeline_cmds[256]; // Array to hold the start of each piped command
    int cmd_count = 0;
    
    token* current = start_token;
    pipeline_cmds[cmd_count++] = current;

    // PHASE 1: Scan the tokens and find all the pipes. 
    // Example: "cat a.txt | grep hello" becomes two chunks in pipeline_cmds.
    while(current != end_token && current != NULL){
        if(current->type == pipe_type){
            if(current->next != end_token && current->next != NULL && current->next->type == word_type){
                pipeline_cmds[cmd_count++] = current->next; 
            }else{
                printf("cshell: invalid syntax\n"); // E.g., "cat |" (missing second command)
                return -1;
            }
        }
        current = current->next;
    }

    int prev_pipe_fd = -1; // Keeps track of the read end of the PREVIOUS pipe.
    pid_t pids[256];       // Keeps track of the Process IDs of all children we spawn.
    pid_t pgid = 0;        // Process Group ID. All commands in this pipeline will share this ID.
    char full_cmd_str[1024] = ""; // We build this string so we can display it in the 'activities' command.

    // PHASE 2: Loop through every command chunk in the pipeline one by one.
    for(int i = 0; i < cmd_count; i++){
        
        // 1. Setup arrays to hold the arguments and file redirections for THIS specific command.
        char* args[256]; int arg_count = 0;
        char* inputs[256]; int input_count = 0;
        char* outputs[256]; int output_append[256]; int output_count = 0;
        int syntax_error = 0;

        // Traverse the tokens for this specific command chunk until we hit the next pipe or the end.
        token* curr = pipeline_cmds[i];
        while(curr != end_token && curr != NULL && curr->type != pipe_type){ 
            if(curr->type == word_type){
                if(arg_count < 255) {
                    args[arg_count++] = curr->value;
                    strcat(full_cmd_str, curr->value); // Add to our display string
                    strcat(full_cmd_str, " ");
                }
            }
            else if(curr->type == less_than_type){ // Input redirection: "<"
                curr = curr->next;
                if(curr && curr->type == word_type) inputs[input_count++] = curr->value;
                else{ syntax_error = 1; break; }
            } 
            else if(curr->type == greater_than_type || curr->type == greater_than_greater_than_type){ // ">" or ">>"
                int is_app = (curr->type == greater_than_greater_than_type);
                curr = curr->next;
                if(curr && curr->type == word_type){
                    outputs[output_count] = curr->value;
                    output_append[output_count++] = is_app; // Remember if it's append (>>) or truncate (>)
                } 
                else{ syntax_error = 1; break; }
            }
            curr = curr->next;
        }
        args[arg_count] = NULL; // Arrays passed to execv must be NULL-terminated.
        if (i < cmd_count - 1) strcat(full_cmd_str, "| "); 

        if (syntax_error) {
            printf("cshell: invalid syntax\n");
            if (prev_pipe_fd != -1) close(prev_pipe_fd);
            return -1;
        }

        // 2. Built-in Intrinsic Detection
        // Check if the command is one of our custom shell functions instead of an external program.
        int is_builtin = 0;
        if (args[0] && (strcmp(args[0], "hop") == 0 || strcmp(args[0], "reveal") == 0 ||
                        strcmp(args[0], "peek") == 0 || strcmp(args[0], "locate") == 0 ||
                        strcmp(args[0], "activities") == 0 || strcmp(args[0], "ping") == 0 ||
                        strcmp(args[0], "resume") == 0 || strcmp(args[0], "spy") == 0 ||
                        strcmp(args[0], "snoop") == 0)) {
            is_builtin = 1;
        }

        // Special Case: "State-modifying" commands like 'hop' (change directory) MUST run in the 
        // parent shell. If we ran them in a child process, the child's directory would change, but 
        // the parent shell would stay in the old directory!
        if (is_builtin && cmd_count == 1) {
            if (strcmp(args[0], "hop") == 0) {
                execute_hop(args, arg_count, home_directory, prev_directory);
                return 0; // Exit pipeline immediately so we don't trigger terminal control and freeze!
            }
            if (strcmp(args[0], "activities") == 0) {
                execute_activities();
                return 0;
            }
            if (strcmp(args[0], "ping") == 0) {
                execute_ping(args, arg_count);
                return 0;
            }
            if (strcmp(args[0], "resume") == 0) {
                execute_resume(args, arg_count);
                return 0;
            }
        }

        char resolved_path[4096];
        int path_found = 0;
        if (!is_builtin) path_found = resolve_command_path(args[0], resolved_path);

        // SEQUENCE ABORTION LOGIC (D1 / C4 constraints)
        // If it's a single external command and it doesn't exist, we must fail immediately.
        if (!is_builtin && !path_found && cmd_count == 1) {
            printf("cshell: command not found (%s)\n", args[0][0] == '%' ? args[0] + 1 : args[0]);
            if (prev_pipe_fd != -1) close(prev_pipe_fd);
            return -1; // Tell parser.c to abort the semicolon sequence
        }

        // 3. Create the OS pipe for inter-process communication.
        // pipe_fd[0] is the read end, pipe_fd[1] is the write end.
        int pipe_fd[2]; 
        if (i < cmd_count - 1) { // We only need a pipe if there is another command after this one.
            if (pipe(pipe_fd) < 0) { perror("cshell"); return -1; }
        }

        // 4. fork() creates an exact duplicate of the current shell process.
        pid_t pid = fork(); 
        
        if(pid < 0){
            perror("cshell");
            return -1;
        }
        else if(pid == 0){ // CHILD PROCESS BLOCK (pid == 0 means we are the child)
            
            // Child processes shouldn't ignore Ctrl-C like the parent does, so we reset signals to Default (SIG_DFL).
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);

            // Grouping: Every pipeline gets its own process group to manage signals easily.
            if (pgid == 0) pgid = getpid(); // If this is the first child, its PID becomes the Group Leader ID.
            setpgid(getpid(), pgid);

            // If it's a foreground job, we hand over control of the physical terminal to this new process group.
            if (!is_background) {
                tcsetpgrp(STDIN_FILENO, pgid);
            }

            // Wire the pipes using dup2! dup2 overwrites standard input (0) or output (1).
            if (prev_pipe_fd != -1) { 
                dup2(prev_pipe_fd, STDIN_FILENO); // Read from the previous pipe instead of the keyboard.
                close(prev_pipe_fd);
            }
            if (i < cmd_count - 1) { 
                dup2(pipe_fd[1], STDOUT_FILENO); // Write to the next pipe instead of the screen.
                close(pipe_fd[0]); // Children don't need the read end of their own output pipe.
                close(pipe_fd[1]);
            }

            if (!is_builtin && !path_found) {
                printf("cshell: command not found (%s)\n", args[0][0] == '%' ? args[0] + 1 : args[0]);
                exit(EXIT_FAILURE); 
            }

            if(!is_builtin && args[0][0] == '%') args[0] = args[0] + 1; 

            // MULTIPLEXER PHASE 1: Input Redirection (<)
            if(input_count > 0){
                FILE* in_tmp = tmpfile(); // Create an invisible, temporary scratch file.
                for(int k = 0; k < input_count; k++){ 
                    int fd = open(inputs[k], O_RDONLY); // Open each input file for reading.
                    if(fd < 0){
                        printf("cshell: no such file or directory\n"); 
                        exit(EXIT_FAILURE);
                    }
                    char buf[4096]; 
                    int bytes;
                    // Read data from the file and dump it into our scratch file.
                    while((bytes = read(fd, buf, sizeof(buf))) > 0) fwrite(buf, 1, bytes, in_tmp); 
                    close(fd);
                }
                fflush(in_tmp); 
                rewind(in_tmp); // Rewind the scratch file to the beginning.
                dup2(fileno(in_tmp), STDIN_FILENO); // Make the command read from this scratch file.
            }

            // MULTIPLEXER PHASE 2: Output Redirection (>, >>)
            if(output_count > 0){
                // First, create/truncate all target files to ensure they exist and have correct permissions.
                for(int k = 0; k < output_count; k++){
                    int flags = O_WRONLY | O_CREAT | (output_append[k] ? O_APPEND : O_TRUNC); 
                    int test_fd = open(outputs[k], flags, 0644); // 0644 means read/write for owner, read for others.
                    if(test_fd < 0){
                        printf("cshell: unable to create file for writing\n"); 
                        exit(EXIT_FAILURE);
                    }
                    close(test_fd); 
                }

                FILE* out_tmp = tmpfile(); // Create a temporary file to catch the command's output.
                int out_fd = fileno(out_tmp);

                // Fork ANOTHER child inside the child (a grand-child!) to run the actual command.
                pid_t gc_pid = fork();
                if (gc_pid == 0) {
                    dup2(out_fd, STDOUT_FILENO); // Direct the grand-child's output into the temp file.
                    
                    if (is_builtin) {
                        // Execute built-ins.
                        if (strcmp(args[0], "reveal") == 0) execute_reveal(args, arg_count, home_directory, prev_directory);
                        else if (strcmp(args[0], "peek") == 0) execute_peek(args, arg_count);
                        else if (strcmp(args[0], "locate") == 0) execute_locate(args, arg_count);
                        else if (strcmp(args[0], "spy") == 0) execute_spy(args, arg_count);
                        else if (strcmp(args[0], "snoop") == 0) execute_snoop(args, arg_count);
                        exit(EXIT_SUCCESS); 
                    } 
                    else {
                        // execv replaces the current process with the new program (e.g., /bin/ls).
                        execv(resolved_path, args); 
                        perror("cshell"); 
                        exit(EXIT_FAILURE);
                    }
                } 
                else {
                    // The middle child waits for the grand-child to finish running.
                    int st; 
                    waitpid(gc_pid, &st, 0);

                    fflush(out_tmp);
                    rewind(out_tmp); // Rewind the temp file so we can read what the grand-child wrote.

                    // Open all the actual user-requested output files.
                    int dest_fds[256];
                    for(int k = 0; k < output_count; k++){ 
                        int flags = O_WRONLY | O_CREAT | (output_append[k] ? O_APPEND : O_TRUNC);
                        dest_fds[k] = open(outputs[k], flags, 0644); 
                    }

                    // Read from the temp file and write identical copies into EVERY output file.
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
                    exit(EXIT_SUCCESS); // Middle child dies.
                }
            } 
            else {
                // If there is NO output multiplexing, just run the command directly.
                if (is_builtin) {
                    if (strcmp(args[0], "reveal") == 0) execute_reveal(args, arg_count, home_directory, prev_directory);
                    else if (strcmp(args[0], "peek") == 0) execute_peek(args, arg_count);
                    else if (strcmp(args[0], "locate") == 0) execute_locate(args, arg_count);
                    else if (strcmp(args[0], "spy") == 0) execute_spy(args, arg_count);
                    else if (strcmp(args[0], "snoop") == 0) execute_snoop(args, arg_count);
                    exit(EXIT_SUCCESS); 
                } 
                else {
                    execv(resolved_path, args); // Overwrite process with the requested program.
                    perror("cshell"); 
                    exit(EXIT_FAILURE);
                }
            }
        } 
        else {
            // PARENT PROCESS BLOCK (C-Shell)
            pids[i] = pid; // Save the child's PID.
            
            if (pgid == 0) pgid = pid; 
            setpgid(pid, pgid); // Ensure the parent also enforces the process group.

            // The parent shell MUST close its copies of the pipes. 
            // If it doesn't, the pipes will stay open forever waiting for the parent to write, and programs will hang.
            if (prev_pipe_fd != -1) close(prev_pipe_fd); 
            if (i < cmd_count - 1) {
                close(pipe_fd[1]); 
                prev_pipe_fd = pipe_fd[0]; // Save the read end so the NEXT loop iteration can hook into it.
            }
        }
    }

    // PHASE 3: Execution Wait State
    // Now that all children are spawned and pipes are wired, what does the parent shell do?
    if (is_background) {
        // If it ends in '&', we just add it to our tracker and immediately print the prompt. We DON'T wait.
        int jid = add_job(pgid, pgid, full_cmd_str);
        printf("[%d] %d\n", jid, pgid);
    } 
    else {
        // Foreground job: The shell gives up the physical terminal.
        tcsetpgrp(STDIN_FILENO, pgid); 

        // Loop through every process we just spawned and wait for them to finish.
        for (int i = 0; i < cmd_count; i++) {
            if (pids[i] > 0) { 
                int status;
                // WUNTRACED tells waitpid to return if the user hits Ctrl-Z (which stops the process).
                waitpid(pids[i], &status, WUNTRACED); 
                
                if (WIFSTOPPED(status)) { // Did the user hit Ctrl-Z?
                    int jid = add_job(pgid, pgid, full_cmd_str);
                    mark_job_stopped(pgid); // Keep tracking it, but mark it as paused.
                    printf("\n[%d] + Stopped %s\n", jid, full_cmd_str);
                    break; 
                }
            }
        }

        // The job is done (or paused). The parent shell reclaims control of the terminal!
        tcsetpgrp(STDIN_FILENO, getpgrp()); 
    }
    
    return 0; // Success
}