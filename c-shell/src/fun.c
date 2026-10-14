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

#ifdef __linux__
#include <dirent.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/reg.h>

static const char* get_file_type(mode_t mode) {
    if (S_ISDIR(mode))  return "DIR";
    if (S_ISREG(mode))  return "REG";
    if (S_ISCHR(mode))  return "CHR";
    if (S_ISFIFO(mode)) return "FIFO";
    if (S_ISSOCK(mode)) return "SOCK";
    if (S_ISBLK(mode))  return "BLK";
    return "UNKNOWN";
}

static void print_spy_entry(pid_t pid, const char* fd_name, const char* link_path) {
    struct stat st;
    if (stat(link_path, &st) == 0) {
        printf("%d\t%s\t%s\t%s\n", pid, fd_name, get_file_type(st.st_mode), link_path);
    }
}

static void resolve_and_print_spy(pid_t pid, const char* fd_name, const char* proc_path) {
    char link_path[4096];
    ssize_t len = readlink(proc_path, link_path, sizeof(link_path) - 1);
    if (len != -1) {
        link_path[len] = '\0';
        print_spy_entry(pid, fd_name, link_path);
    }
}
#endif

/*
 * 'spy' command: inspects open file descriptors, executable paths,
 * and memory mappings of a specified process ID.
 */
int execute_spy(char** args, int arg_count) {
    if (arg_count > 2) {
        printf("spy: invalid syntax\n");
        return -1;
    }

    pid_t target_pid = (arg_count == 2) ? atoi(args[1]) : getpid();

    /* Verify process existence and permissions */
    if (kill(target_pid, 0) == -1) {
        if (errno == EPERM) printf("spy: permission denied\n");
        else printf("spy: no such process\n");
        return -1;
    }

#ifdef __APPLE__
    /* macOS fallback: parse lsof for target PID */
    printf("[macOS Fallback] Simulating /proc inspection via lsof...\n");
    pid_t p = fork();
    if (p == 0) {
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d", target_pid);
        execlp("lsof", "lsof", "-p", pid_str, NULL);
        exit(1);
    } else {
        waitpid(p, NULL, 0);
    }
    return 0;
#else
    printf("PID\tFD\tTYPE\tPATH\n");

    char path_buf[512];

    /* Working directory */
    snprintf(path_buf, sizeof(path_buf), "/proc/%d/cwd", target_pid);
    resolve_and_print_spy(target_pid, "cwd", path_buf);

    /* Executable binary */
    snprintf(path_buf, sizeof(path_buf), "/proc/%d/exe", target_pid);
    resolve_and_print_spy(target_pid, "txt", path_buf);

    /* Memory-mapped files */
    snprintf(path_buf, sizeof(path_buf), "/proc/%d/maps", target_pid);
    FILE* maps_file = fopen(path_buf, "r");
    if (maps_file) {
        char line[1024];
        char last_map[1024] = "";
        while (fgets(line, sizeof(line), maps_file)) {
            char map_path[1024] = "";
            sscanf(line, "%*s %*s %*s %*s %*s %1023s", map_path);
            if (strlen(map_path) > 0 && map_path[0] == '/') {
                if (strcmp(map_path, last_map) != 0) {
                    print_spy_entry(target_pid, "mem", map_path);
                    strcpy(last_map, map_path);
                }
            }
        }
        fclose(maps_file);
    }

    /* Open file descriptors */
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

typedef struct {
    long id;
    int count;
    double total_time;
    int first_occurrence;
} SyscallStat;

#ifdef __linux__
static const char* get_syscall_name(long id) {
    switch (id) {
        case 0:   return "read";
        case 1:   return "write";
        case 2:   return "open";
        case 3:   return "close";
        case 4:   return "stat";
        case 5:   return "fstat";
        case 9:   return "mmap";
        case 10:  return "mprotect";
        case 11:  return "munmap";
        case 12:  return "brk";
        case 35:  return "nanosleep";
        case 39:  return "getpid";
        case 57:  return "fork";
        case 59:  return "execve";
        case 60:  return "exit";
        case 61:  return "wait4";
        case 231: return "exit_group";
        case 257: return "openat";
        default:  return NULL;
    }
}

static int compare_syscalls(const void* a, const void* b) {
    const SyscallStat* s1 = (const SyscallStat*)a;
    const SyscallStat* s2 = (const SyscallStat*)b;
    if (s1->count != s2->count) return s2->count - s1->count;
    return s1->first_occurrence - s2->first_occurrence;
}
#endif

/*
 * 'snoop' command: system call profiler using ptrace to track call
 * frequencies and cumulative execution times.
 */
int execute_snoop(char** args, int arg_count) {
    if (arg_count < 2) {
        printf("snoop: invalid syntax\n");
        return -1;
    }

#ifdef __APPLE__
    fprintf(stderr, "[macOS Fallback] ptrace syscall tracing is Linux-specific. Executing directly.\n");
    if (strcmp(args[1], "-p") == 0) return -1;
    pid_t p = fork();
    if (p == 0) {
        execvp(args[1], &args[1]);
        exit(1);
    } else {
        waitpid(p, NULL, 0);
    }
    return 0;
#else
    pid_t trace_pid = -1;

    if (strcmp(args[1], "-p") == 0) {
        if (arg_count != 3) {
            printf("snoop: invalid syntax\n");
            return -1;
        }
        trace_pid = atoi(args[2]);
        if (ptrace(PTRACE_ATTACH, trace_pid, NULL, NULL) < 0) {
            printf("snoop: no such process\n");
            return -1;
        }
    } else {
        trace_pid = fork();
        if (trace_pid == 0) {
            ptrace(PTRACE_TRACEME, 0, NULL, NULL);
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

    while (1) {
        ptrace(PTRACE_SYSCALL, trace_pid, NULL, NULL);
        waitpid(trace_pid, &status, 0);
        if (WIFEXITED(status) || WIFSIGNALED(status)) break;

        ptrace(PTRACE_GETREGS, trace_pid, NULL, &regs);
        long syscall_id = regs.orig_rax;

        clock_gettime(CLOCK_MONOTONIC, &start);

        ptrace(PTRACE_SYSCALL, trace_pid, NULL, NULL);
        waitpid(trace_pid, &status, 0);
        if (WIFEXITED(status) || WIFSIGNALED(status)) break;

        clock_gettime(CLOCK_MONOTONIC, &end);

        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

        int found = 0;
        for (int i = 0; i < unique_syscalls; i++) {
            if (stats[i].id == syscall_id) {
                stats[i].count++;
                stats[i].total_time += elapsed;
                found = 1;
                break;
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

    qsort(stats, unique_syscalls, sizeof(SyscallStat), compare_syscalls);

    fprintf(stderr, "%-15s %-7s %s\n", "syscall", "calls", "time");
    for (int i = 0; i < unique_syscalls; i++) {
        const char* name = get_syscall_name(stats[i].id);
        if (name) {
            fprintf(stderr, "%-15s %-7d %.3fs\n", name, stats[i].count, stats[i].total_time);
        } else {
            char fb_name[32];
            snprintf(fb_name, sizeof(fb_name), "syscall_%ld", stats[i].id);
            fprintf(stderr, "%-15s %-7d %.3fs\n", fb_name, stats[i].count, stats[i].total_time);
        }
    }
    return 0;
#endif
}