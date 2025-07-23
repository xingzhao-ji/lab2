#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

int main(int argc, char *argv[]) {
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
    
    int (*pipes)[2] = malloc((num_programs - 1) * sizeof(int[2]));
    if (!pipes && num_programs > 1) {
        perror("malloc");
        free(pids);
        exit(errno);
    }
    
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
    
    for (int i = 0; i < num_programs; i++) {
        pids[i] = fork();
        
        if (pids[i] == -1) {
            perror("fork");
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
            if (i > 0) {
                // Not the first process - read from previous pipe
                if (dup2(pipes[i-1][0], STDIN_FILENO) == -1) {
                    perror("dup2 input");
                    exit(errno);
                }
            }
            
            if (i < num_programs - 1) {
                // Not the last process - write to next pipe
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2 output");
                    exit(errno);
                }
            }
            
            for (int j = 0; j < num_programs - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            execlp(argv[i + 1], argv[i + 1], (char *)NULL);
            
            perror("execlp");
            exit(errno);
        }
    }
    
    for (int i = 0; i < num_programs - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    int exit_status = 0;
    for (int i = 0; i < num_programs; i++) {
        int status;
        if (waitpid(pids[i], &status, 0) == -1) {
            perror("waitpid");
            exit_status = errno;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            exit_status = WEXITSTATUS(status);
        }
    }
    
    free(pipes);
    free(pids);
    
    exit(exit_status);
}
