#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include "../include/jobs.h"

Job job_list[1024];
int next_job_id = 1;

void init_jobs(void) {
    for (int i = 0; i < 1024; i++) {
        job_list[i].is_active = 0;
    }
}

int add_job(pid_t pgid, pid_t lead_pid, const char* cmd) {
    for (int i = 0; i < 1024; i++) {
        if (!job_list[i].is_active) {
            job_list[i].job_id = next_job_id++;
            job_list[i].pgid = pgid;
            job_list[i].lead_pid = lead_pid;
            strncpy(job_list[i].command_name, cmd, 1023);
            job_list[i].command_name[1023] = '\0';
            job_list[i].is_stopped = 0;
            job_list[i].is_active = 1;
            return job_list[i].job_id;
        }
    }
    return -1;
}

void remove_job(pid_t pgid) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].pgid == pgid) {
            job_list[i].is_active = 0;
            return;
        }
    }
}

void mark_job_stopped(pid_t pgid) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].pgid == pgid) {
            job_list[i].is_stopped = 1;
        }
    }
}

void mark_job_running(pid_t pgid) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].pgid == pgid) {
            job_list[i].is_stopped = 0;
        }
    }
}

/*
 * Asynchronously reaps finished background processes and updates
 * status without blocking the interactive shell prompt.
 */
void check_background_jobs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        for (int i = 0; i < 1024; i++) {
            if (job_list[i].is_active && job_list[i].pgid == pid) {
                if (WIFEXITED(status)) {
                    printf("\n%s with pid %d exited normally\n", job_list[i].command_name, pid);
                    remove_job(pid);
                } else if (WIFSIGNALED(status)) {
                    printf("\n%s with pid %d exited abnormally\n", job_list[i].command_name, pid);
                    remove_job(pid);
                } else if (WIFSTOPPED(status)) {
                    mark_job_stopped(pid);
                }
                break;
            }
        }
    }
}

/*
 * 'activities' builtin: displays all active background and suspended jobs.
 */
int execute_activities(void) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active) {
            printf("[%d] pgid %d\n", job_list[i].job_id, job_list[i].pgid);
            printf("  %d %s %s\n", job_list[i].lead_pid, job_list[i].command_name,
                   job_list[i].is_stopped ? "Stopped" : "Running");
        }
    }
    return 0;
}

/*
 * 'ping' builtin: sends signal to a process or process group (%job_id or raw pid).
 */
int execute_ping(char** args, int arg_count) {
    if (arg_count != 3) {
        printf("ping: invalid syntax\n");
        return -1;
    }

    int sig = atoi(args[2]);
    if (sig < 0) {
        printf("ping: invalid syntax\n");
        return -1;
    }
    sig = sig % 64;

    pid_t target_pid = -1;

    if (args[1][0] == '%') {
        int target_job = atoi(&args[1][1]);
        for (int i = 0; i < 1024; i++) {
            if (job_list[i].is_active && job_list[i].job_id == target_job) {
                target_pid = -job_list[i].pgid;
                break;
            }
        }
    } else {
        pid_t raw_pid = atoi(args[1]);
        for (int i = 0; i < 1024; i++) {
            if (job_list[i].is_active && job_list[i].pgid == raw_pid) {
                target_pid = raw_pid;
                break;
            }
        }
    }

    if (target_pid == -1) {
        printf("ping: no such process found\n");
        return -1;
    }

    if (kill(target_pid, sig) == 0) {
        printf("Sent signal %s to %s\n", args[2], args[1]);
    } else {
        perror("cshell");
    }
    return 0;
}

/*
 * 'resume' builtin: moves a stopped or background job to foreground ('fg') or background ('bg').
 */
int execute_resume(char** args, int arg_count) {
    if (arg_count < 3 || args[1][0] != '%') {
        printf("resume: invalid syntax\n");
        return -1;
    }

    int target_job = atoi(&args[1][1]);
    pid_t target_pgid = -1;
    char cmd_name[1024];

    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].job_id == target_job) {
            target_pgid = job_list[i].pgid;
            strcpy(cmd_name, job_list[i].command_name);
            break;
        }
    }

    if (target_pgid == -1) {
        printf("resume: no such job\n");
        return -1;
    }

    if (strcmp(args[2], "bg") == 0) {
        mark_job_running(target_pgid);
        kill(-target_pgid, SIGCONT);
        printf("[%d] + Running %s\n", target_job, cmd_name);
    } else if (strcmp(args[2], "fg") == 0) {
        printf("%s\n", cmd_name);
        mark_job_running(target_pgid);

        tcsetpgrp(STDIN_FILENO, target_pgid);
        kill(-target_pgid, SIGCONT);

        int status;
        waitpid(target_pgid, &status, WUNTRACED);

        tcsetpgrp(STDIN_FILENO, getpgrp());

        if (WIFSTOPPED(status)) {
            mark_job_stopped(target_pgid);
            printf("\n[%d] + Stopped %s\n", target_job, cmd_name);
        } else {
            remove_job(target_pgid);
        }
    } else {
        printf("resume: invalid syntax\n");
    }
    return 0;
}