#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

typedef struct {
    int job_id; //monotonically increasing session-wide id like [1], [2]
    pid_t pgid; //process group id for the entire pipeline
    pid_t lead_pid; //pid of the very first command in the pipeline
    char command_name[1024]; //string holding what the user typed
    int is_stopped; //boolean 1 if stopped by ctrl-z, 0 if running
    int is_active; //boolean 1 if job exists, 0 if empty slot
} Job;

extern Job job_list[1024]; //global array to hold up to 1024 background/stopped jobs
extern int next_job_id;

void init_jobs();
int add_job(pid_t pgid, pid_t lead_pid, const char* cmd);
void remove_job(pid_t pgid);
void mark_job_stopped(pid_t pgid);
void mark_job_running(pid_t pgid);
void check_background_jobs();

//part e intrinsics
int execute_activities();
int execute_ping(char** args, int arg_count);
int execute_resume(char** args, int arg_count);

#endif