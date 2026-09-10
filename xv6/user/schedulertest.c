#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    printf("Starting MLFQ Scheduler Test...\n");
    printf("Spam Ctrl+P now!\n");

    // Spawn 3 processes with varying CPU burst lengths
    for (int i = 0; i < 3; i++) {
        int pid = fork();
        if (pid == 0) {
            // Massive nested loops to force the processes to stay alive for several seconds
            // Process 0 does the least work, Process 2 does the most.
            int outer_loops = 300 * (i + 1);
            for(int k = 0; k < outer_loops; k++) {
                for(volatile int j = 0; j < 10000000; j++) {
                    // Heavy computation to eat up time slices
                }
            }
            printf("Process %d finished burst.\n", i);
            exit(0);
        }
    }

    // Parent waits for all 3 children to finish
    wait(0);
    wait(0);
    wait(0);
    
    printf("Test complete!\n");
    exit(0);
}