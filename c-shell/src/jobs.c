#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "../include/jobs.h"

// A global array acting as our database of up to 1024 background/stopped processes.
Job job_list[1024];
int next_job_id = 1; // Job numbers [1], [2], etc., increase monotonically and are never reused per session.

void init_jobs() {
    // Before the shell starts, mark all 1024 slots as empty.
    for (int i = 0; i < 1024; i++) job_list[i].is_active = 0; 
}

// Registers a new process in the database.
int add_job(pid_t pgid, pid_t lead_pid, const char* cmd) {
    for (int i = 0; i < 1024; i++) { // Scan array for the first empty slot.
        if (!job_list[i].is_active) {
            job_list[i].job_id = next_job_id++; // Assign ID and immediately increment for the next job.
            job_list[i].pgid = pgid;            // Process Group ID.
            job_list[i].lead_pid = lead_pid;
            strncpy(job_list[i].command_name, cmd, 1023); // Save the string (e.g., "sleep 100")
            job_list[i].is_stopped = 0;         // It starts out running, not stopped.
            job_list[i].is_active = 1;          // Mark slot as occupied.
            return job_list[i].job_id;
        }
    }
    return -1; // Array full failsafe.
}

// Frees up a slot when a process finishes.
void remove_job(pid_t pgid) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].pgid == pgid) {
            job_list[i].is_active = 0; 
            return;
        }
    }
}

// Flags a process as paused (e.g., when the user hits Ctrl-Z).
void mark_job_stopped(pid_t pgid) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].pgid == pgid) {
            job_list[i].is_stopped = 1; 
        }
    }
}

// Flags a paused process as running again (e.g., via 'bg' or 'fg').
void mark_job_running(pid_t pgid) {
    for (int i = 0; i < 1024; i++) {
        if (job_list[i].is_active && job_list[i].pgid == pgid) {
            job_list[i].is_stopped = 0; 
        }
    }
}

// This is the most crucial function in this file. It is called repeatedly in main.c's while loop.
void check_background_jobs() {
    int status;
    pid_t pid;
    
    // waitpid(-1, ...) means "check EVERY child process attached to this shell".
    // WNOHANG is vital! It tells waitpid to return immediately if no child has exited, preventing the shell from freezing.
    // WUNTRACED tells it to also report children that were stopped by Ctrl-Z, not just killed children.
    // The loop runs until waitpid returns 0 (no more updates) or -1 (no children exist).
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        for (int i = 0; i < 1024; i++) {
            if (job_list[i].is_active && job_list[i].pgid == pid) {
                
                // Macros provided by sys/wait.h to analyze the 'status' integer.
                if (WIFEXITED(status)) { // The process finished all by itself successfully.
                    printf("\n%s with pid %d exited normally\n", job_list[i].command_name, pid);
                    remove_job(pid);
                } else if (WIFSIGNALED(status)) { // The process was assassinated (e.g., by 'kill' command).
                    printf("\n%s with pid %d exited abnormally\n", job_list[i].command_name, pid);
                    remove_job(pid);
                } else if (WIFSTOPPED(status)) { // The process was suspended (Ctrl-Z).
                    mark_job_stopped(pid);
                }
                break;
            }
        }
    }
}

// The 'activities' built-in command simply prints the contents of our array.
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

// The 'ping' built-in command is used to send UNIX signals to processes.
int execute_ping(char** args, int arg_count) {
    if (arg_count != 3) {
        printf("ping: invalid syntax\n");
        return -1;
    }
    
    int sig = atoi(args[2]); // Convert the signal string to an integer.
    if (sig < 0) {
        printf("ping: invalid syntax\n"); // Rubric says negative numbers are invalid syntax.
        return -1;
    }
    sig = sig % 64; // Rubric requirement: mod 64 math for safety.

    pid_t target_pid = -1;
    
    // Check if the user targeted a job ID (e.g., "ping %2")
    if (args[1][0] == '%') { 
        int target_job = atoi(&args[1][1]); // Skip the '%' character.
        for (int i = 0; i < 1024; i++) {
            if (job_list[i].is_active && job_list[i].job_id == target_job) {
                // By making the PID negative in kill(), the OS sends the signal to the ENTIRE process group.
                target_pid = -job_list[i].pgid; 
                break;
            }
        }
    } else { // The user targeted a raw PID (e.g., "ping 4032")
        pid_t raw_pid = atoi(args[1]);
        for (int i = 0; i < 1024; i++) {
            // Rubric constraint: We can only ping processes that this shell actually spawned.
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

    // Execute the OS kill() system call to send the signal.
    if (kill(target_pid, sig) == 0) {
        // Rubric note: We must print the ORIGINAL typed signal (args[2]), not the mod 64 version.
        printf("Sent signal %s to %s\n", args[2], args[1]); 
    } else {
        perror("cshell");
    }
    return 0;
}

// The 'resume' built-in command manages foregrounding (fg) or backgrounding (bg) suspended jobs.
int execute_resume(char** args, int arg_count) {
    if (arg_count < 3 || args[1][0] != '%') { // Must target a job, e.g., "resume %1 fg"
        printf("resume: invalid syntax\n");
        return -1;
    }
    
    int target_job = atoi(&args[1][1]); 
    pid_t target_pgid = -1;
    char cmd_name[1024];

    // Find the job in the database.
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

    // BG: Continue the process, but leave it running invisibly in the background.
    if (strcmp(args[2], "bg") == 0) {
        mark_job_running(target_pgid);
        kill(-target_pgid, SIGCONT); // SIGCONT tells a suspended process to wake up and resume.
        printf("[%d] + Running %s\n", target_job, cmd_name);
    } 
    // FG: Wake the process up, but give it control of the physical terminal so the user can interact with it.
    else if (strcmp(args[2], "fg") == 0) {
        printf("%s\n", cmd_name);
        mark_job_running(target_pgid);
        
        tcsetpgrp(STDIN_FILENO, target_pgid); // Hand over terminal control.
        kill(-target_pgid, SIGCONT);          // Wake it up.

        int status;
        waitpid(target_pgid, &status, WUNTRACED); // Pause the shell and wait for the job to finish or stop.
        
        tcsetpgrp(STDIN_FILENO, getpgrp()); // The job stopped/finished. Reclaim terminal control safely!

        if (WIFSTOPPED(status)) {
            mark_job_stopped(target_pgid); // User hit Ctrl-Z again. Update tracker.
            printf("\n[%d] + Stopped %s\n", target_job, cmd_name);
        } else {
            remove_job(target_pgid); // It finished naturally. Remove it from tracker.
        }
    } 
    else {
        printf("resume: invalid syntax\n");
    }
    return 0;
}