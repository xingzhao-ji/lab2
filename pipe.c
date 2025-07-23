#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>

static int status_map(int s) {
    if (WIFSIGNALED(s)) {
        int sig = WTERMSIG(s);
        return sig == SIGPIPE ? 0 : 128 + sig;
    }
    int c = WEXITSTATUS(s);
    return c == 128 + SIGPIPE ? 0 : c;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        errno = EINVAL;
        perror("pipe");
        exit(errno);
    }

    if (argc == 2) {
        execlp(argv[1], argv[1], (char *)0);
        perror(argv[1]);
        exit(errno);
    }

    int prev_pipe[2] = {-1, -1};
    pid_t *pids = malloc((argc - 1) * sizeof(pid_t));
    int *codes  = malloc((argc - 1) * sizeof(int));
    if (!pids || !codes) exit(ENOMEM);
    for (int i = 0; i < argc - 1; ++i) codes[i] = -1;

    for (int i = 1; i < argc; ++i) {
        int curr_pipe[2] = {-1, -1};
        if (i < argc - 1 && pipe(curr_pipe) == -1) {
            perror("pipe");
            exit(errno);
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(errno);
        }

        if (pid == 0) {
            if (i > 1 && dup2(prev_pipe[0], STDIN_FILENO) == -1) _exit(errno);
            if (i < argc - 1 && dup2(curr_pipe[1], STDOUT_FILENO) == -1) _exit(errno);

            if (prev_pipe[0] != -1) { close(prev_pipe[0]); close(prev_pipe[1]); }
            if (curr_pipe[0] != -1) { close(curr_pipe[0]); close(curr_pipe[1]); }

            execlp(argv[i], argv[i], (char *)0);
            perror(argv[i]);
            _exit(errno);
        }

        pids[i - 1] = pid;

        if (prev_pipe[0] != -1) { close(prev_pipe[0]); close(prev_pipe[1]); }
        if (i < argc - 1) {
            prev_pipe[0] = curr_pipe[0];
            prev_pipe[1] = curr_pipe[1];
            close(curr_pipe[1]);          /* parent keeps only read end */
        }
    }

    int remaining = argc - 1;
    while (remaining) {
        int st;
        pid_t w = wait(&st);
        if (w == -1) {
            if (errno == EINTR) continue;
            perror("wait");
            exit(errno);
        }
        for (int k = 0; k < argc - 1; ++k)
            if (pids[k] == w && codes[k] == -1) { codes[k] = status_map(st); break; }
        --remaining;
    }

    int rc = 0;
    for (int k = 0; k < argc - 1; ++k)
        if (codes[k]) { rc = codes[k]; break; }

    return rc;
}
