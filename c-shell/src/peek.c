// Standard C libraries for I/O, memory, and string manipulation[cite: 4].
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Libraries for low-level POSIX file operations (open, read, lseek) and file information (stat)[cite: 4].
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
// Custom header for peek definitions[cite: 4].
#include "../include/peek.h"

// Core function that actually reads and prints the file's contents[cite: 4].
void peek_file(const char* filename, int n_flag, int r_flag){
    // Determine if we are reading from standard input (like a pipe) instead of a file on disk[cite: 4].
    int is_stdin = (strcmp(filename, "-") == 0); 

    // CASE 1: Reading from standard input (stdin) AND we don't need to reverse it[cite: 4].
    // This is the easiest case: just read line by line and print[cite: 4].
    if(is_stdin && !r_flag){ 
        char *line = NULL; // getline will allocate memory for us[cite: 4].
        size_t len = 0; 
        int line_num = 1;  // Start line counter at 1[cite: 4].
        
        // Loop until there is no more input (EOF)[cite: 4].
        while(getline(&line, &len, stdin) != -1){ 
            // If the user passed '-n', print the line number first[cite: 4].
            if(n_flag) printf("%d ", line_num++);
            printf("%s", line); // Print the actual text[cite: 4].
        }
        free(line); // Prevent memory leaks[cite: 4].
        return; // We are done with this file[cite: 4].
    }

    int fd; // "File Descriptor": an integer the OS uses to identify an open file[cite: 4].
    FILE *tmp = NULL; 

    // CASE 2: Reading from standard input, BUT we need to reverse it (-r)[cite: 4].
    if(is_stdin){
        // Problem: You cannot read a stream (like keyboard input) backward[cite: 4].
        // Solution: Save the incoming stream to a temporary file on the disk first[cite: 4].
        tmp = tmpfile(); // Creates an invisible, temporary file that auto-deletes when closed[cite: 4].
        if(!tmp){ perror("cshell"); return; } // Error handling[cite: 4].
        
        char buf[4096];
        int bytes;
        // Read chunks from stdin and write them to our temporary file[cite: 4].
        while((bytes = read(STDIN_FILENO, buf, sizeof(buf))) > 0){ 
            fwrite(buf, 1, bytes, tmp); 
        }
        fflush(tmp); // Force the data to be written to disk[cite: 4].
        
        // Grab the file descriptor for our temp file and rewind it to the beginning[cite: 4].
        fd = dup(fileno(tmp)); 
        lseek(fd, 0, SEEK_SET); 
    }
    else{
        // CASE 3: Reading a normal file from the disk[cite: 4].
        struct stat path_stat; 
        
        // 'stat' checks if the file exists and gets its metadata[cite: 4].
        if(stat(filename, &path_stat) != 0){
            printf("peek: no such file or directory\n"); // Doesn't exist[cite: 4].
            return;
        }
        // 'S_ISDIR' checks the metadata to see if it's a folder. We can't read a folder like text[cite: 4].
        if(S_ISDIR(path_stat.st_mode)){
            printf("peek: is a directory\n");
            return;
        }
        
        // Open the file in Read-Only mode[cite: 4].
        fd = open(filename, O_RDONLY); 
        if(fd < 0) { // Safety check[cite: 4].
            printf("peek: no such file or directory\n");
            return;
        }
    }
    
    // AT THIS POINT: 'fd' holds our file (whether it was a real file or our temp stdin file)[cite: 4].

    if(!r_flag){ 
        // We do NOT need to reverse. Just read normally forward[cite: 4].
        FILE *f = fdopen(fd, "r"); // Convert our low-level 'fd' into a standard C 'FILE' stream[cite: 4].
        char *line = NULL;
        size_t len = 0;
        int line_num = 1;
        
        while(getline(&line, &len, f) != -1){ 
            if (n_flag) printf("%d ", line_num++);
            printf("%s", line);
        }
        free(line);
        fclose(f); // Close the file[cite: 4].
    }
    else{ 
        // We DO need to reverse. This is the hardest part[cite: 4].
        // Because files can be huge (e.g., 5GB), we can't load the whole thing into memory[cite: 4].
        // We have to use 'lseek' to jump to the end of the file and read it backward in 4096-byte chunks[cite: 4].
        
        int total_lines = 0;
        lseek(fd, 0, SEEK_SET); // Go to the start[cite: 4].
        char buf[4096];
        int bytes;
        char last_char = '\n';
        
        // First pass: We must count the total number of lines so we can number them backward if '-n' is used[cite: 4].
        while((bytes = read(fd, buf, sizeof(buf))) > 0){ 
            for(int j = 0; j < bytes; j++) {
                if(buf[j] == '\n') total_lines++; // Count every newline character[cite: 4].
            }
            last_char = buf[bytes - 1]; // Keep track of the very last character[cite: 4].
        }
        
        off_t original_file_size = lseek(fd, 0, SEEK_END); // Jump to the end and get the file size in bytes[cite: 4].
        
        // If the file doesn't end in a newline, we add 1 to account for that final dangling line[cite: 4].
        if(last_char != '\n' && original_file_size > 0) total_lines++;

        off_t current_pos = original_file_size; // Start our reading position at the very end[cite: 4].
        int lines_to_print = total_lines;
        int line_num = total_lines; // Start numbering from the maximum line count[cite: 4].

        // 'line_tail' helps stitch words together if a word is cut in half between two 4096-byte chunks[cite: 4].
        char *line_tail = malloc(1); 
        line_tail[0] = '\0'; 
        int tail_len = 0;

        // Second pass: Read backwards chunk by chunk[cite: 4].
        while(current_pos > 0){ 
            off_t bytes_to_read = 4096; 
            // If we have less than 4096 bytes left to the start of the file, just read what's left[cite: 4].
            if(current_pos < 4096) bytes_to_read = current_pos; 
            current_pos -= bytes_to_read; // Move our position backwards[cite: 4].

            lseek(fd, current_pos, SEEK_SET); // Jump to the new position[cite: 4].
            read(fd, buf, bytes_to_read); // Read a chunk[cite: 4].

            int last_boundary = bytes_to_read; 

            // Scan the chunk backwards looking for newlines ('\n')[cite: 4].
            for(int i = bytes_to_read - 1; i >= 0; i--){
                if(buf[i] == '\n'){
                    // If this is the absolute end of the file, just mark the boundary and ignore[cite: 4].
                    if(current_pos + i == original_file_size - 1) {
                        last_boundary = i;
                    }
                    else{
                        // We found a complete line! Print its line number if requested[cite: 4].
                        if(n_flag) printf("%d ", line_num--);
                        
                        // Print the characters from right after the newline up to the boundary[cite: 4].
                        fwrite(&buf[i + 1], 1, last_boundary - 1 - i, stdout); 

                        // If there was leftover text from the previous chunk, stitch it on the end[cite: 4].
                        if(tail_len > 0){ 
                            fwrite(line_tail, 1, tail_len, stdout);
                            tail_len = 0;
                        }
                        printf("\n");
                        last_boundary = i; // Update boundary[cite: 4].
                        lines_to_print--;
                    }
                }
            }
            
            // If there is text left before the first newline in this chunk, save it to 'line_tail' 
            // so we can stitch it onto the next chunk we read[cite: 4].
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
        
        // Print the very first line of the file (which is the last one processed in reverse)[cite: 4].
        if(lines_to_print > 0){ 
            if(n_flag) printf("%d ", line_num--);
            if(tail_len > 0) fwrite(line_tail, 1, tail_len, stdout);
            printf("\n");
        }
        free(line_tail);
        close(fd); // Cleanup[cite: 4].
    }
    if(tmp) fclose(tmp); // If we used a temp file for stdin, close and delete it[cite: 4].
}

// Router function called by the parser to set up the peek execution[cite: 4].
int execute_peek(char** args, int arg_count){
    int n_flag = 0, r_flag = 0, invalid_syntax = 0;
    char* targets[256]; 
    int target_count = 0;

    // Loop through all typed arguments to separate flags (like -n, -r) from filenames (targets)[cite: 4].
    for (int i = 1; i < arg_count; i++) {
        // If it starts with '-' and has letters, it's a flag[cite: 4].
        if (args[i][0] == '-' && strlen(args[i]) > 1){ 
            for (int j = 1; args[i][j] != '\0'; j++) { 
                if (args[i][j] == 'n') n_flag = 1;
                else if (args[i][j] == 'r') r_flag = 1;
                else invalid_syntax = 1; // Unrecognized flag[cite: 4].
            }
        }
        else{
            // Otherwise, it's a filename. Save it to our targets array[cite: 4].
            if (target_count < 256) targets[target_count++] = args[i];
        }
    }

    if (invalid_syntax) { // Abort if they passed a bad flag[cite: 4].
        printf("peek: invalid syntax\n");
        return -1;
    }

    // If no targets were provided, default to reading standard input ("-")[cite: 4].
    if(target_count == 0){ 
        peek_file("-", n_flag, r_flag);
    }
    else{
        // For every filename provided, call our core function[cite: 4].
        for (int i = 0; i < target_count; i++) {
            // Note: If a file is missing, peek_file prints an error, but this loop keeps going to the next file[cite: 4].
            peek_file(targets[i], n_flag, r_flag); 
        }
    }
    return 0; // Success[cite: 4].
}