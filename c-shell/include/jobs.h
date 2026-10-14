#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

typedef struct {
    int job_id;
    pid_t pgid;
    pid_t lead_pid;
    char command_name[1024];
    int is_stopped;
    int is_active;
} Job;

extern Job job_list[1024];
extern int next_job_id;

void init_jobs(void);
int add_job(pid_t pgid, pid_t lead_pid, const char* cmd);
void remove_job(pid_t pgid);
void mark_job_stopped(pid_t pgid);
void mark_job_running(pid_t pgid);
void check_background_jobs(void);

int execute_activities(void);
int execute_ping(char** args, int arg_count);
int execute_resume(char** args, int arg_count);

#endif