#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../include/peek.h"

void peek_file(const char* filename, int n_flag, int r_flag){
    int is_stdin = (strcmp(filename, "-") == 0); //checks if the input should be read as standard input or not

    if(is_stdin && !r_flag){ //reading input from std input without the reverse flag
        char *line = NULL;
        size_t len = 0; 
        int line_num = 1;
        while(getline(&line, &len, stdin) != -1){ 
            if(n_flag) printf("%d ", line_num++);
            printf("%s", line);
        }
        free(line);
        return;
    }

    int fd; 
    FILE *tmp = NULL; 

    if(is_stdin){
        tmp = tmpfile(); 
        if(!tmp){ perror("cshell"); return; }
        
        char buf[4096];
        int bytes;
        while((bytes = read(STDIN_FILENO, buf, sizeof(buf))) > 0){ 
            fwrite(buf, 1, bytes, tmp); 
        }
        fflush(tmp); 
        fd = dup(fileno(tmp)); 
        lseek(fd, 0, SEEK_SET); 
    }
    else{
        struct stat path_stat; 
        if(stat(filename, &path_stat) != 0){
            printf("peek: no such file or directory\n");
            return;
        }
        if(S_ISDIR(path_stat.st_mode)){
            printf("peek: is a directory\n");
            return;
        }
        fd = open(filename, O_RDONLY); 
        if(fd < 0) {
            printf("peek: no such file or directory\n");
            return;
        }
    }
    
    if(!r_flag){ 
        FILE *f = fdopen(fd, "r");
        char *line = NULL;
        size_t len = 0;
        int line_num = 1;
        while(getline(&line, &len, f) != -1){ 
            if (n_flag) printf("%d ", line_num++);
            printf("%s", line);
        }
        free(line);
        fclose(f);
    }
    else{ 
        int total_lines = 0;
        lseek(fd, 0, SEEK_SET); 
        char buf[4096];
        int bytes;
        char last_char = '\n';
        
        while((bytes = read(fd, buf, sizeof(buf))) > 0){ 
            for(int j = 0; j < bytes; j++) {
                if(buf[j] == '\n') total_lines++;
            }
            last_char = buf[bytes - 1]; 
        }
        
        off_t original_file_size = lseek(fd, 0, SEEK_END);
        if(last_char != '\n' && original_file_size > 0) total_lines++;

        off_t current_pos = original_file_size; 
        int lines_to_print = total_lines;
        int line_num = total_lines;

        char *line_tail = malloc(1); 
        line_tail[0] = '\0'; 
        int tail_len = 0;

        while(current_pos > 0){ 
            off_t bytes_to_read = 4096; 
            if(current_pos < 4096) bytes_to_read = current_pos; 
            current_pos -= bytes_to_read;

            lseek(fd, current_pos, SEEK_SET);
            read(fd, buf, bytes_to_read);

            int last_boundary = bytes_to_read; 

            for(int i = bytes_to_read - 1; i >= 0; i--){
                if(buf[i] == '\n'){
                    if(current_pos + i == original_file_size - 1) {
                        last_boundary = i;
                    }
                    else{
                        if(n_flag) printf("%d ", line_num--);
                        fwrite(&buf[i + 1], 1, last_boundary - 1 - i, stdout); 

                        if(tail_len > 0){ 
                            fwrite(line_tail, 1, tail_len, stdout);
                            tail_len = 0;
                        }
                        printf("\n");
                        last_boundary = i;
                        lines_to_print--;
                    }
                }
            }
            if(last_boundary > 0){ 
                char *new_tail = malloc(last_boundary + tail_len + 1); 
                memcpy(new_tail, buf, last_boundary);
                memcpy(new_tail + last_boundary, line_tail, tail_len);
                new_tail[last_boundary + tail_len] = '\0';
                free(line_tail);
                line_tail = new_tail;
                tail_len = last_boundary + tail_len;
            }
        }
        if(lines_to_print > 0){ 
            if(n_flag) printf("%d ", line_num--);
            if(tail_len > 0) fwrite(line_tail, 1, tail_len, stdout);
            printf("\n");
        }
        free(line_tail);
        close(fd);
    }
    if(tmp) fclose(tmp); 
}

int execute_peek(char** args, int arg_count){
    int n_flag = 0, r_flag = 0, invalid_syntax = 0;
    char* targets[256]; 
    int target_count = 0;

    for (int i = 1; i < arg_count; i++) {
        if (args[i][0] == '-' && strlen(args[i]) > 1){ 
            for (int j = 1; args[i][j] != '\0'; j++) { 
                if (args[i][j] == 'n') n_flag = 1;
                else if (args[i][j] == 'r') r_flag = 1;
                else invalid_syntax = 1; 
            }
        }
        else{
            if (target_count < 256) targets[target_count++] = args[i];
        }
    }

    if (invalid_syntax) {
        printf("peek: invalid syntax\n");
        return -1;
    }

    if(target_count == 0){ 
        peek_file("-", n_flag, r_flag);
    }
    else{
        for (int i = 0; i < target_count; i++) {
            //spec requirement: if a file is missing, print error and keep looping!
            peek_file(targets[i], n_flag, r_flag); 
        }
    }
    return 0;
}