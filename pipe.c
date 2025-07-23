#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

static void close_all(int (*pipes)[2], int n) {
    for (int i = 0; i < n; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        errno = EINVAL;
        exit(EINVAL);
    }

    const int n = argc - 1;

    pid_t *pids = malloc((size_t)n * sizeof *pids);
    if (!pids) {
        perror("malloc pids");
        exit(errno);
    }

    int (*pipes)[2] = NULL;
    if (n > 1) {
        pipes = malloc((size_t)(n - 1) * sizeof(int[2]));
        if (!pipes) {
            perror("malloc pipes");
            free(pids);
            exit(errno);
        }
        for (int i = 0; i < n - 1; ++i) {
            if (pipe(pipes[i]) == -1) {
                perror("pipe");
                close_all(pipes, i);
                free(pipes);
                free(pids);
                exit(errno);
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        pids[i] = fork();
        if (pids[i] == -1) {
            perror("fork");
            if (pipes) close_all(pipes, n - 1);
            for (int j = 0; j < i; ++j) waitpid(pids[j], NULL, 0);
            free(pipes);
            free(pids);
            exit(errno);
        }

        if (pids[i] == 0) {
            if (n > 1) {
                if (i > 0 && dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
                    perror("dup2 stdin");
                    exit(errno);
                }
                if (i < n - 1 && dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2 stdout");
                    exit(errno);
                }
                close_all(pipes, n - 1);
            }
            execvp(argv[i + 1], &argv[i + 1]);
            perror("execvp");
            exit(errno);
        }
    }

    if (pipes) close_all(pipes, n - 1);

    int exit_status = 0;
    for (int i = 0; i < n; ++i) {
        int st;
        if (waitpid(pids[i], &st, 0) == -1) {
            if (!exit_status) exit_status = errno;
        } else if (WIFEXITED(st)) {
            int s = WEXITSTATUS(st);
            if (s && !exit_status) exit_status = s;
        }
    }

    free(pipes);
    free(pids);

    exit(exit_status);
}
