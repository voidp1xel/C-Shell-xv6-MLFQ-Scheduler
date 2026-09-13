#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include "../include/fun.h"

// We use conditional compilation (#ifdef) because macOS doesn't have a /proc folder or ptrace.
#ifdef __linux__
#include <dirent.h>
// ptrace allows one process to control and inspect the internal state of another process.
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/reg.h>

// Helper to translate Linux file mode integers into readable strings.
const char* get_file_type(mode_t mode) {
    if (S_ISDIR(mode)) return "DIR";   // Directory
    if (S_ISREG(mode)) return "REG";   // Regular file
    if (S_ISCHR(mode)) return "CHR";   // Character device (like a terminal)
    if (S_ISFIFO(mode)) return "FIFO"; // Pipe
    if (S_ISSOCK(mode)) return "SOCK"; // Network socket
    if (S_ISBLK(mode)) return "BLK";   // Block device (like a hard drive)
    return "UNKNOWN";
}

// Helper to print the formatted output for the spy command.
void print_spy_entry(pid_t pid, const char* fd_name, const char* link_path) {
    struct stat st;
    if (stat(link_path, &st) == 0) { // stat() gets file metadata like type and permissions.
        printf("%d\t%s\t%s\t%s\n", pid, fd_name, get_file_type(st.st_mode), link_path);
    }
}

// In Linux /proc, file descriptors are symlinks. readlink() follows the symlink to get the real path.
void resolve_and_print_spy(pid_t pid, const char* fd_name, const char* proc_path) {
    char link_path[4096];
    ssize_t len = readlink(proc_path, link_path, sizeof(link_path) - 1);
    if (len != -1) {
        link_path[len] = '\0';
        print_spy_entry(pid, fd_name, link_path);
    }
}
#endif

// --- SPY IMPLEMENTATION ---
int execute_spy(char** args, int arg_count) {
    if (arg_count > 2) {
        printf("spy: invalid syntax\n");
        return -1;
    }

    // If they typed "spy", default to tracking the shell's own PID. Else, use the provided PID.
    pid_t target_pid = (arg_count == 2) ? atoi(args[1]) : getpid(); 

    // Sending signal 0 with kill() doesn't actually kill the process; it just checks if the 
    // process exists and if we have permission to interact with it.
    if (kill(target_pid, 0) == -1) {
        if (errno == EPERM) printf("spy: permission denied\n");
        else printf("spy: no such process\n");
        return -1;
    }

#ifdef __APPLE__
    // macOS doesn't have /proc, so we just run the system 'lsof' (List Open Files) command instead.
    printf("[macOS Fallback] macOS lacks /proc. Simulating with lsof...\n");
    pid_t p = fork();
    if (p == 0) {
        char pid_str[32]; snprintf(pid_str, sizeof(pid_str), "%d", target_pid);
        execlp("lsof", "lsof", "-p", pid_str, NULL);
        exit(1);
    } else { waitpid(p, NULL, 0); }
    return 0;
#else
    // Linux implementation reading directly from the virtual /proc filesystem.
    printf("PID\tFD\tTYPE\tPATH\n");

    char path_buf[512];
    
    // Get Current Working Directory of the process.
    snprintf(path_buf, sizeof(path_buf), "/proc/%d/cwd", target_pid);
    resolve_and_print_spy(target_pid, "cwd", path_buf);

    // Get the Executable path of the process.
    snprintf(path_buf, sizeof(path_buf), "/proc/%d/exe", target_pid);
    resolve_and_print_spy(target_pid, "txt", path_buf);

    // Parse the memory maps file to find loaded libraries (like libc).
    snprintf(path_buf, sizeof(path_buf), "/proc/%d/maps", target_pid);
    FILE* maps_file = fopen(path_buf, "r");
    if (maps_file) {
        char line[1024]; char last_map[1024] = ""; 
        while (fgets(line, sizeof(line), maps_file)) {
            char map_path[1024] = "";
            // Extract just the file path from the long memory map line.
            sscanf(line, "%*s %*s %*s %*s %*s %1023s", map_path); 
            if (strlen(map_path) > 0 && map_path[0] == '/') {
                if (strcmp(map_path, last_map) != 0) { // Avoid printing the same library 10 times.
                    print_spy_entry(target_pid, "mem", map_path);
                    strcpy(last_map, map_path);
                }
            }
        }
        fclose(maps_file);
    }

    // Loop through the /proc/[pid]/fd directory to list all standard open files (0, 1, 2, etc).
    snprintf(path_buf, sizeof(path_buf), "/proc/%d/fd", target_pid);
    DIR* fd_dir = opendir(path_buf);
    if (fd_dir) {
        struct dirent* entry;
        while ((entry = readdir(fd_dir)) != NULL) {
            if (entry->d_name[0] != '.') {
                char fd_path[1024];
                snprintf(fd_path, sizeof(fd_path), "%s/%s", path_buf, entry->d_name);
                resolve_and_print_spy(target_pid, entry->d_name, fd_path);
            }
        }
        closedir(fd_dir);
    }
    return 0;
#endif
}

// === SNOOP IMPLEMENTATION ===

// Structure to track how many times a system call happens and how long it takes.
typedef struct {
    long id;
    int count;
    double total_time;
    int first_occurrence; 
} SyscallStat;

#ifdef __linux__
// Maps raw system call integer IDs (from CPU registers) to readable string names.
const char* get_syscall_name(long id) {
    switch(id) {
        case 0: return "read"; case 1: return "write"; case 2: return "open";
        case 3: return "close"; case 4: return "stat"; case 5: return "fstat";
        case 9: return "mmap"; case 10: return "mprotect"; case 11: return "munmap";
        case 12: return "brk"; case 35: return "nanosleep"; case 39: return "getpid";
        case 57: return "fork"; case 59: return "execve"; case 60: return "exit";
        case 61: return "wait4"; case 231: return "exit_group"; case 257: return "openat";
        default: return NULL;
    }
}[cite: 1]

// Sorting logic for qsort(): Sort by call count (highest to lowest), then by first occurrence.
int compare_syscalls(const void* a, const void* b) {
    SyscallStat* s1 = (SyscallStat*)a;
    SyscallStat* s2 = (SyscallStat*)b;
    if (s1->count != s2->count) return s2->count - s1->count; 
    return s1->first_occurrence - s2->first_occurrence;
}
#endif

int execute_snoop(char** args, int arg_count) {
    if (arg_count < 2) {
        printf("snoop: invalid syntax\n");
        return -1;
    }

#ifdef __APPLE__
    // macOS doesn't support ptrace, so we just run the command normally so the shell doesn't break.
    fprintf(stderr, "[macOS Fallback] macOS does not support ptrace syscalls. Running command normally.\n");
    if (strcmp(args[1], "-p") == 0) return -1;
    pid_t p = fork();
    if (p == 0) { execvp(args[1], &args[1]); exit(1); } 
    else { waitpid(p, NULL, 0); }
    return 0;
#else
    pid_t trace_pid = -1;

    // Attach to an existing running process using -p.
    if (strcmp(args[1], "-p") == 0) {
        if (arg_count != 3) { printf("snoop: invalid syntax\n"); return -1; }
        trace_pid = atoi(args[2]);
        if (ptrace(PTRACE_ATTACH, trace_pid, NULL, NULL) < 0) { // Hook into the process!
            printf("snoop: no such process\n");
            return -1;
        }
    } 
    else {
        // Start a brand new process and trace it from the beginning.
        trace_pid = fork();
        if (trace_pid == 0) {
            ptrace(PTRACE_TRACEME, 0, NULL, NULL); // Tell the OS: "My parent is going to trace me!"
            execvp(args[1], &args[1]); 
            printf("snoop: command not found\n"); 
            exit(EXIT_FAILURE);
        }
    }

    int status;
    waitpid(trace_pid, &status, 0); 

    SyscallStat stats[500] = {0}; 
    int unique_syscalls = 0;
    int occ_cnt = 0;
    struct user_regs_struct regs;
    struct timespec start, end;

    // This loop acts like a debugger, stepping forward every time the program makes a system call.
    while (1) {
        ptrace(PTRACE_SYSCALL, trace_pid, NULL, NULL); // Let process run until its next syscall ENTRY.
        waitpid(trace_pid, &status, 0);
        if (WIFEXITED(status) || WIFSIGNALED(status)) break; // Stop if program died.

        ptrace(PTRACE_GETREGS, trace_pid, NULL, &regs); // Extract CPU registers to see WHICH syscall it is.
        long syscall_id = regs.orig_rax; // On x86-64, the syscall ID is stored in the RAX register.
        
        clock_gettime(CLOCK_MONOTONIC, &start); // Start the stopwatch.

        ptrace(PTRACE_SYSCALL, trace_pid, NULL, NULL); // Let process run until syscall EXIT.
        waitpid(trace_pid, &status, 0);
        if (WIFEXITED(status) || WIFSIGNALED(status)) break;

        clock_gettime(CLOCK_MONOTONIC, &end); // Stop the stopwatch.
        
        // Calculate math for elapsed seconds.
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

        // Log the data in our stats array.
        int found = 0;
        for (int i = 0; i < unique_syscalls; i++) {
            if (stats[i].id == syscall_id) {
                stats[i].count++; stats[i].total_time += elapsed;
                found = 1; break;
            }
        }
        if (!found && unique_syscalls < 500) {
            stats[unique_syscalls].id = syscall_id;
            stats[unique_syscalls].count = 1;
            stats[unique_syscalls].total_time = elapsed;
            stats[unique_syscalls].first_occurrence = occ_cnt++;
            unique_syscalls++;
        }
    }

    // Sort the results using the helper function defined above.
    qsort(stats, unique_syscalls, sizeof(SyscallStat), compare_syscalls);

    // Print the summary table to stderr per rubric.
    fprintf(stderr, "%-15s %-7s %s\n", "syscall", "calls", "time");[cite: 1]
    for (int i = 0; i < unique_syscalls; i++) {
        const char* name = get_syscall_name(stats[i].id);
        if (name) {
            fprintf(stderr, "%-15s %-7d %.3fs\n", name, stats[i].count, stats[i].total_time);[cite: 1]
        } else {
            char fb_name[32]; snprintf(fb_name, sizeof(fb_name), "syscall_%ld", stats[i].id);
            fprintf(stderr, "%-15s %-7d %.3fs\n", fb_name, stats[i].count, stats[i].total_time);[cite: 1]
        }
    }
    return 0;
#endif
}