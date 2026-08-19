#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../include/peek.h"

void peek_file(const char* filename, int n_flag, int r_flag){
    int is_stdin = (strcmp(filename, "-") == 0); //checks if the input should be read as standard input or not(in the wrapper function we have put an if else condition on this)

    if(is_stdin && !r_flag){ //reading input from std input wihtout the reverse flag
        char *line = NULL;
        size_t len = 0; //size_t is used to adapt to host machine and to store potentially large numbers
        int line_num = 1;
        while(getline(&line, &len, stdin) != -1){ //reads from terminal or pipe until EOF
            if(n_flag) printf("%d ", line_num++);
            printf("%s", line);
        }
        free(line);
        return;
    }

    int fd; //file descriptor
    FILE *tmp = NULL; //pointer for file

    if(is_stdin){
        tmp = tmpfile(); //creates a temp file on storage and deletes when program closes
        if(!tmp){ 
            perror("cshell"); 
            return; 
        }
        char buf[4096];
        int bytes;
        while((bytes = read(STDIN_FILENO, buf, sizeof(buf))) > 0){ //read the entire infinite stream of std input in blocks of 4096 bytes
            fwrite(buf, 1, bytes, tmp); //write these blocks onto the temp ghost file
        }
        fflush(tmp); //tells os to write data in storage rther than holding it in ram
        fd = dup(fileno(tmp)); //fileno -> raw integer fd of our temp file. dup -> duplicator. temp fd stored in the int fd
        lseek(fd, 0, SEEK_SET); //because the pointer is currently at end of file, we need to get it at the beginning
    }
    else{
        struct stat path_stat; //the file
        if(stat(filename, &path_stat) != 0){
            printf("peek: no such file or directory\n");
            return;
        }
        if(S_ISDIR(path_stat.st_mode)){
            printf("peek: is a directory\n");
            return;
        }
        fd = open(filename, O_RDONLY); //opens file in read only mode and gives us fd of the file.
        if(fd < 0) {
            printf("peek: no such file or directory\n");
            return;
        }
    }
    if(!r_flag){ //reverse order not wanted
        FILE *f = fdopen(fd, "r");
        char *line = NULL;
        size_t len = 0;
        int line_num = 1;
        while(getline(&line, &len, f) != -1){ //requires a FILE* , not a raw fd pointer
            if (n_flag) printf("%d ", line_num++);
            printf("%s", line);
        }
        free(line);
        fclose(f);
    }
    else{ //if the user types -rn, we need to know total no of lines to start the number eg 43 
        int total_lines = 0;
        lseek(fd, 0, SEEK_SET); //puts pointer at beginning
        char buf[4096];
        int bytes;
        char last_char = '\n';
        
        while((bytes = read(fd, buf, sizeof(buf))) > 0){ //read bytes in 4096 chunks
            for(int j = 0; j < bytes; j++) {
                if(buf[j] == '\n') total_lines++;
            }
            last_char = buf[bytes - 1]; //saving the last character we read
        }
        
        off_t original_file_size = lseek(fd, 0, SEEK_END);

        if(last_char != '\n' && original_file_size > 0){
            total_lines++;
        }

        off_t current_pos = original_file_size; //offset type -> represents file size and position of cursor in file
        int lines_to_print = total_lines;
        int line_num = total_lines;

        char *line_tail = malloc(1); //allocate and free 1 byte ram
        line_tail[0] = '\0'; //creates empty string. hold right half of cut sentence from previously read chunk
        int tail_len = 0;

        while(current_pos > 0){ //reverse reading loop
            off_t bytes_to_read = 4096; //defining chunk size
            if(current_pos < 4096) bytes_to_read = current_pos; //if less than 4096, prevents us from reading from the beginning of the file(negative pointer.)
            current_pos -= bytes_to_read;

            lseek(fd, current_pos, SEEK_SET);
            read(fd, buf, bytes_to_read);

            int last_boundary = bytes_to_read; //marks edge of our chunk or in other words tracks right edge of the line we want to print

            for(int i = bytes_to_read - 1; i >= 0; i--){
                if(buf[i] == '\n'){
                    if(current_pos + i == original_file_size - 1) {//if last letter of text file is \n, we dont want to create a new line for it
                        last_boundary = i;//shifted to newline to skip printing
                    }
                    else{
                        if(n_flag) printf("%d ", line_num--);

                        fwrite(&buf[i + 1], 1, last_boundary - 1 - i, stdout); //newline found at i. so to print next line i+1

                        if(tail_len > 0){ //if text saved from preevious line, we print it now to complete senetence
                            fwrite(line_tail, 1, tail_len, stdout);
                            tail_len = 0;
                        }
                        printf("\n");
                        last_boundary = i;
                        lines_to_print--;
                    }
                }
            }
            if(last_boundary > 0){ //text left from 0 to last boundary after for loop. this means this is the right half of the senetence
                char *new_tail = malloc(last_boundary + tail_len + 1); //data structure in memory used to combine new cut text with text in line_tail properly
                memcpy(new_tail, buf, last_boundary);
                memcpy(new_tail + last_boundary, line_tail, tail_len);
                new_tail[last_boundary + tail_len] = '\0';
                free(line_tail);
                line_tail = new_tail;
                tail_len = last_boundary + tail_len;
            }
        }
        if(lines_to_print > 0){ //printing the topmost line as it doesnt have a \n in front of it
            if(n_flag) printf("%d ", line_num--);
            if(tail_len > 0) fwrite(line_tail, 1, tail_len, stdout);
            printf("\n");
        }
        free(line_tail);
        close(fd);
    }
    if(tmp){
        fclose(tmp); //deletes the temporary file
    }
}

int execute_peek(int n_flag, int r_flag, char** targets, int target_count){
    if(target_count == 0){ //the case when user enter no file name. it considers this as '-' and passes it to function. this is treated as read from std input
        peek_file("-", n_flag, r_flag);
    }
    else{
        for (int i = 0; i < target_count; i++) {
            peek_file(targets[i], n_flag, r_flag); //loops in order and peeks the files ione by one
        }
    }
    return 0;
}
