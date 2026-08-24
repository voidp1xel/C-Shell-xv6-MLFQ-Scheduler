# C-Shell (OSN Mini Project 1)

A custom, POSIX-compliant UNIX shell implemented entirely in C from scratch. This repository contains the Mid-Submission deliverables (Parts A, B, and C), featuring a custom lexer/parser, built-in intrinsic commands, I/O multiplexing, and unlimited command piping.

## 🚀 Compilation & Execution

A `Makefile` is provided to compile the shell using strict POSIX flags (`-std=c23 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700`).

**To compile the shell, run:**
```bash
make