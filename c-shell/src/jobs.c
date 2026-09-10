#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "../include/jobs.h"

Job job_list[1024];
int next_job_id = 1; //starts at 1 and increases forever during this session

void init_jobs() {
    for (int i = 0; i < 1024; i++) job_list[i].is_active = 0; //mark all slots as empty initially
}

int add_job(pid_t pgid, pid_t lead_pid, const char* cmd) {
    for (int i = 0; i < 1024; i++) { //find first empty slot
        if (!job_list[i].is_active) {
            job_list[i].job_id = next_job_id++; //assign id and increment for the next guy
            job_list[i].pgid = pgid;
            job_list[i].lead_pid = lead_pid;
            strncpy(job_list[i].command_name, cmd, 1023);
            job_list[i].is_stopped = 0;
            job_list[i].is_active = 1;
            return job_list[i].job_id;
        }
    }
    return -1; //array full
}

void remove_job(pid_t pgid) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].pgid == pgid) {
            job_list[i].is_active = 0; //freed the slot
            return;
        }
    }
}

void mark_job_stopped(pid_t pgid) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].pgid == pgid) {
            job_list[i].is_stopped = 1; //user pressed ctrl-z
        }
    }
}

void mark_job_running(pid_t pgid) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].pgid == pgid) {
            job_list[i].is_stopped = 0; //user ran bg or fg
        }
    }
}

void check_background_jobs() {
    int status;
    pid_t pid;
    //WNOHANG tells waitpid to return immediately if no child has exited. prevents shell from freezing!
    //WUNTRACED tells it to also report children that were stopped by ctrl-z
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        for (int i = 0; i < 1024; i++) {
            if (job_list[i].is_active && job_list[i].pgid == pid) {
                if (WIFEXITED(status)) { //exited on its own normally
                    printf("\n%s with pid %d exited normally\n", job_list[i].command_name, pid);
                    remove_job(pid);
                } else if (WIFSIGNALED(status)) { //killed by a signal like ctrl-c or kill
                    printf("\n%s with pid %d exited abnormally\n", job_list[i].command_name, pid);
                    remove_job(pid);
                } else if (WIFSTOPPED(status)) { //paused by ctrl-z
                    mark_job_stopped(pid);
                }
                break;
            }
        }
    }
}

int execute_activities() {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active) {
            printf("[%d] pgid %d\n", job_list[i].job_id, job_list[i].pgid);
            printf("  %d %s %s\n", job_list[i].lead_pid, job_list[i].command_name, 
                   job_list[i].is_stopped ? "Stopped" : "Running");
        }
    }
    return 0;
}

int execute_ping(char** args, int arg_count) {
    if (arg_count != 3) {
        printf("ping: invalid syntax\n");
        return -1;
    }
    int sig = atoi(args[2]);
    if (sig < 0) {
        printf("ping: invalid syntax\n"); //spec says negative numbers are invalid syntax
        return -1;
    }
    sig = sig % 64; //spec requirement for signal math

    pid_t target_pid = -1;
    if (args[1][0] == '%') { //user gave a job id like %2
        int target_job = atoi(&args[1][1]);
        for (int i = 0; i < 1024; i++) {
            if (job_list[i].is_active && job_list[i].job_id == target_job) {
                target_pid = -job_list[i].pgid; //negative pid sends signal to the entire process group
                break;
            }
        }
    } else { //user gave raw pid
        pid_t raw_pid = atoi(args[1]);
        for (int i = 0; i < 1024; i++) {
            if (job_list[i].is_active && job_list[i].pgid == raw_pid) { //spec says we can only ping processes the shell spawned!
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
        printf("Sent signal %s to %s\n", args[2], args[1]); //spec says echo original typed signal, not the modulo one
    } else {
        perror("cshell");
    }
    return 0;
}

int execute_resume(char** args, int arg_count) {
    if (arg_count < 3 || args[1][0] != '%') {
        printf("resume: invalid syntax\n");
        return -1;
    }
    
    int target_job = atoi(&args[1][1]); //extract number after %
    pid_t target_pgid = -1;
    char cmd_name[1024];

    for (int i = 0; i < 1024; i++) { //find the job
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
        kill(-target_pgid, SIGCONT); //send continue signal to entire group
        printf("[%d] + Running %s\n", target_job, cmd_name);
    } 
    else if (strcmp(args[2], "fg") == 0) {
        printf("%s\n", cmd_name);
        mark_job_running(target_pgid);
        
        tcsetpgrp(STDIN_FILENO, target_pgid); //hand over terminal control to this process group
        kill(-target_pgid, SIGCONT);

        int status;
        waitpid(target_pgid, &status, WUNTRACED); //wait for it to finish or get stopped again
        
        tcsetpgrp(STDIN_FILENO, getpgrp()); //reclaim terminal safely using the process group!

        if (WIFSTOPPED(status)) {
            mark_job_stopped(target_pgid);
            printf("\n[%d] + Stopped %s\n", target_job, cmd_name);
        } else {
            remove_job(target_pgid); //it finished naturally
        }
    } 
    else {
        printf("resume: invalid syntax\n");
    }
    return 0;
}