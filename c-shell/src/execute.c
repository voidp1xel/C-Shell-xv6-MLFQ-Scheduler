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

int execute_external_command(char** args, char** inputs, int input_count, char** outputs, int* output_append, int output_count) {
    char resolved_path[4096];

    if(!resolve_command_path(args[0], resolved_path)){ //finding program
        printf("cshell: command not found (%s)\n", args[0]);
        return -1;
    }

    //multiplexer phase 1: input combination
    FILE* in_tmp = NULL; //glues a.txt and b.txt into this file and later we give to the command
    int in_fd = -1;

    if(input_count > 0){
        in_tmp = tmpfile(); //create hidden file to hold all combined inputs
        for(int i = 0; i < input_count; i++){ //loop through every input file provided by user eg a.txt, b.txt. open them in read only mode
            int fd = open(inputs[i], O_RDONLY);
            if(fd < 0){
                printf("cshell: no such file or directory\n"); //spec requirement if input file is missing
                if(in_tmp) fclose(in_tmp);
                return -1;
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
        in_fd = fileno(in_tmp); //grab raw integer file descriptor
    }

    //multiplexer phase 2: output pre-check
    //we check permissions now so we can abort before forking if user doesnt have write access
    for(int i = 0; i < output_count; i++){
        int flags = O_WRONLY | O_CREAT; //flags are binary in c
        if(output_append[i]) flags |= O_APPEND; //bitwise OR operator to add append flag. output_append[i]=1 only if >> found
        else flags |= O_TRUNC; //trunc flag deletes all existing text in file
        
        int test_fd = open(outputs[i], flags, 0644); //0644 gives rw-r--r-- permissions
        if(test_fd < 0){
            printf("cshell: unable to create file for writing\n"); //spec requirement
            if(in_tmp) fclose(in_tmp);
            return -1;
        }
        close(test_fd); //close it immediately, we will reopen it later to write actual data
    }

    FILE* out_tmp = NULL;
    int out_fd = -1;

    if(output_count > 0){
        out_tmp = tmpfile(); //hidden file to capture the commands output
        out_fd = fileno(out_tmp);
    }

    pid_t pid = fork(); //needed to hold id of running process

    if(pid < 0){
        perror("cshell");
        if(in_tmp) fclose(in_tmp);
        if(out_tmp) fclose(out_tmp);
        return -1;
    }

    else if(pid == 0){ //inside child fork as pid = 0
        if(args[0][0] == '%'){ //if user types %ls or sum like that, we move pointer forward to make it think user type ls
            args[0] = args[0] + 1;
        }

        //dup2 forces standard streams to point to our hidden files instead of the terminal screen
        if(in_fd != -1) dup2(in_fd, STDIN_FILENO); //dup connects one std input/output to file at a time
        if(out_fd != -1) dup2(out_fd, STDOUT_FILENO);

        execv(resolved_path, args); //exec command called. if it works, child process is replaced by new data

        perror("cshell"); //since child did not get replaced by new data, we knew the exec command failed
        exit(EXIT_FAILURE);
    }

    else{
        int status;
        waitpid(pid, &status, 0); //wait until process with id = pid terminates

        //multiplexer phase 3: distribute captured output to all requested files
        if(output_count > 0){
            fflush(out_tmp);
            rewind(out_tmp); //rewind our capture file to the beginning

            int dest_fds[256];
            for(int i = 0; i < output_count; i++){ //open all destination files at once
                int flags = O_WRONLY | O_CREAT;
                if(output_append[i]) flags |= O_APPEND;
                else flags |= O_TRUNC;
                dest_fds[i] = open(outputs[i], flags, 0644); 
            }

            char buf[4096];
            int bytes;
            while((bytes = fread(buf, 1, sizeof(buf), out_tmp)) > 0){ //read chunk from capture file(trapped output 4096 sized chunks at a time)
                for(int i = 0; i < output_count; i++){ //loop to write exact same chunk to every file user requested
                    if(dest_fds[i] >= 0){
                        write(dest_fds[i], buf, bytes); //write exact chunk to every user requested file
                    }
                }
            }

            for(int i = 0; i < output_count; i++){
                if(dest_fds[i] >= 0) close(dest_fds[i]); //close all target files
            }
        }

        if(in_tmp) fclose(in_tmp);
        if(out_tmp) fclose(out_tmp); //deletes the invisible temp files automatically
    }

    return 0;
}