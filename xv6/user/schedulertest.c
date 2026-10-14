#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/*
 * Workload generator for the Multi-Level Feedback Queue (MLFQ) scheduler.
 * Spawns compute-intensive processes with staggered CPU burst durations
 * to verify time-slice demotion across queues and periodic priority boosting.
 */
int main(void) {
    printf("Starting MLFQ Scheduler Test...\n");
    printf("Press Ctrl+P to observe process queue demotion (q:) and ticks used (t:)\n");

    for (int i = 0; i < 3; i++) {
        int pid = fork();
        if (pid == 0) {
            int outer_loops = 300 * (i + 1);
            for (int k = 0; k < outer_loops; k++) {
                for (volatile int j = 0; j < 10000000; j++) {
                    /* Compute-bound CPU workload */
                }
            }
            printf("Process %d finished burst.\n", i);
            exit(0);
        }
    }

    wait(0);
    wait(0);
    wait(0);

    printf("MLFQ workload completed.\n");
    exit(0);
}