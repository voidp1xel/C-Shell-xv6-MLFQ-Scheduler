# Mini Project 1: C-Shell & xv6 MLFQ Scheduler
**Author:** Aniket Goel
**Course:** Operating Systems

This repository contains the implementation of a custom POSIX-compliant C-Shell and a Multi-Level Feedback Queue (MLFQ) scheduler integrated into the xv6 operating system.

---

## 1. How to Run the Code

### C-Shell Execution
Navigate to the C-Shell directory to build and run the interactive shell:
```bash
cd c-shell
make clean
make all
./shell.out
```

### xv6 Execution (Default Round-Robin)
To boot xv6 using the default Round-Robin scheduler:
```bash
cd xv6
make clean
make qemu
```

### xv6 Execution (MLFQ Scheduler)
To compile and boot xv6 using the custom MLFQ scheduler:
```bash
cd xv6
make clean
make qemu SCHEDULER=MLFQ
```
Once booted, you can run the custom evaluation program by typing `schedulertest` at the xv6 prompt and pressing `Ctrl+P` to observe queue demotion and priority boosting.

---

## 2. Folder Structure

```text
mini-project1/
├── c-shell/
│   ├── src/                 # Source files for lexer, parser, and command execution
│   ├── include/             # Header files defining function prototypes and structures
│   └── Makefile             # Build instructions for C-Shell
├── xv6/
│   ├── user/                # User-space programs (including schedulertest.c)
│   ├── mkfs/                # File system generation tools
│   ├── kernel/              # Core xv6 kernel files (scheduler, trap, proc, etc.)
│   ├── Makefile             # Build instructions (modified for SCHEDULER flag)
│   ├── scheduler_plot.py    # Python script for generating MLFQ analysis plots
│   └── report.md            # Analysis report and generated plots
├── AI-usage.pdf             # Documentation of AI assistance
└── README.md                # This file
```

---

## 3. Design Choices

### C-Shell
*   **Lexical Analysis:** Implemented a Finite State Machine (FSM) to scan input, reliably grouping string literals inside single (`'`) and double (`"`) quotes while ignoring arbitrary whitespace outside them.
*   **Sequential Abortion:** Designed the execution router to strictly abort remaining sequences (the `;` operator) if a command fails to start, but to continue execution if a command starts but exits with an error status.
*   **Frecency Engine:** `hop` utilizes a localized, hidden CSV file (`.cshell_frecency`) saved in the user's home directory. It dynamically tracks visit frequency to allow fuzzy-path jumping.
*   **Piping & Multiplexing:** `pipe()` file descriptors are generated dynamically in a loop. Standard output multiplexing (`> f1 > f2`) is handled seamlessly by dumping child output to a temporary file, which the parent then duplicates into all specified targets.
*   **macOS Fallbacks (`spy` / `snoop`):** Since Apple Silicon/macOS lacks the Linux `/proc` filesystem and `ptrace` system calls, I implemented `#ifdef __APPLE__` preprocessor macros. On macOS, `spy` simulates `/proc` behavior by parsing `lsof` output, while `snoop` safely bypasses `ptrace` tracing to prevent crashes during local development, ensuring seamless Linux compatibility for grading.
*   **Process Control:** Terminal control is safely handed to foreground jobs using `tcsetpgrp()`, while background processes are reaped silently via a `SIGCHLD` handler using `waitpid(-1, &status, WNOHANG)` to prevent terminal freezing.

---

## 4. Key Changes Made to xv6 (MLFQ)

*   **`struct proc` & `allocproc()`:** Added two integer fields to the process control block: `queue` (tracking the current priority level, 0-3) and `ticks_used` (tracking time consumed in the current slice). Both are initialized to `0` upon process creation.
*   **`scheduler()`:** Completely rewrote the core scheduling loop to strictly execute `RUNNABLE` processes from the highest available priority queue.
*   **Time-Slice Demotion (`trap.c`):** Integrated a tick counter into hardware timer interrupts. When a process exceeds its queue's respective limit (1, 4, 8, or 16 ticks), it is demoted to the next lower queue. Preemption is enforced by calling `yield()` on every single tick.
*   **Anti-Starvation Boost:** Added a global `last_boost_tick` tracker. Every 48 ticks, the scheduler locks the process table and forcefully resets the `queue` and `ticks_used` of all active processes to `0`.
*   **Voluntary Yielding:** Modified `sleep()` so that processes yielding the CPU for I/O reset their `ticks_used` but maintain their current `queue` level.
*   **`procdump()`:** Altered the output format (triggered by `Ctrl+P`) to include `q:` (queue) and `t:` (ticks_used) to visibly confirm scheduler mechanics.

---

## 5. Assumptions

1.  **Command Length:** Assumes a maximum command length of 1024 characters per the assignment specification.
2.  **`spy` Target Validity:** Assumes `spy` is provided with valid, active PIDs. Attempting to track dead or inaccessible processes gracefully outputs a failure message.
3.  **Frecency Tie-Breakers:** If two directories in the `hop` frecency log share the exact same score and substring match, the algorithm assumes the first one discovered in the CSV sequence is the intended target.
4.  **Python Plotting:** Assumes `matplotlib` and `numpy` are installed in the evaluator's local Python environment to run `scheduler_plot.py`.