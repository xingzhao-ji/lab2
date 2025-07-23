#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    // Check if at least one program is provided
    if (argc < 2) {
        errno = EINVAL;
        exit(EINVAL);
    }
    
    int num_programs = argc - 1;
    pid_t *pids = malloc(num_programs * sizeof(pid_t));
    if (!pids) {
        perror("malloc");
        exit(errno);
    }
    
    // Create pipes - we need (num_programs - 1) pipes
    int (*pipes)[2] = malloc((num_programs - 1) * sizeof(int[2]));
    if (!pipes && num_programs > 1) {
        perror("malloc");
        free(pids);
        exit(errno);
    }
    
    // Create all pipes before forking
    for (int i = 0; i < num_programs - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            // Clean up previously created pipes
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            free(pipes);
            free(pids);
            exit(errno);
        }
    }
    
    // Fork and execute each program
    for (int i = 0; i < num_programs; i++) {
        pids[i] = fork();
        
        if (pids[i] == -1) {
            perror("fork");
            // Clean up pipes and wait for already created children
            for (int j = 0; j < num_programs - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            for (int j = 0; j < i; j++) {
                waitpid(pids[j], NULL, 0);
            }
            free(pipes);
            free(pids);
            exit(errno);
        }
        
        if (pids[i] == 0) {
            // Child process
            
            // Set up input redirection
            if (i > 0) {
                // Not the first process - read from previous pipe
                if (dup2(pipes[i-1][0], STDIN_FILENO) == -1) {
                    perror("dup2 input");
                    exit(errno);
                }
            }
            // First process uses parent's stdin (no redirection needed)
            
            // Set up output redirection
            if (i < num_programs - 1) {
                // Not the last process - write to next pipe
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2 output");
                    exit(errno);
                }
            }
            // Last process uses parent's stdout (no redirection needed)
            
            // Close all pipe file descriptors in child
            for (int j = 0; j < num_programs - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            // Execute the program
            execlp(argv[i + 1], argv[i + 1], (char *)NULL);
            
            // If we reach here, execlp failed
            perror("execlp");
            exit(errno);
        }
    }
    
    // Parent process - close all pipe file descriptors
    for (int i = 0; i < num_programs - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all children
    int exit_status = 0;
    for (int i = 0; i < num_programs; i++) {
        int status;
        if (waitpid(pids[i], &status, 0) == -1) {
            perror("waitpid");
            exit_status = errno;
        }
        // Optionally check child exit status
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            exit_status = WEXITSTATUS(status);
        }
    }
    
    // Clean up
    free(pipes);
    free(pids);
    
    exit(exit_status);
}
