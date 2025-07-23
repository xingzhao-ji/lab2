#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>

static int map_status(int s) {
    if (WIFSIGNALED(s)) return WTERMSIG(s) == SIGPIPE ? 0 : 128 + WTERMSIG(s);
    int c = WEXITSTATUS(s);
    return c == 128 + SIGPIPE ? 0 : c;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) exit(EINVAL);

    int *start = malloc((argc - 1) * sizeof(int));
    int *len   = malloc((argc - 1) * sizeof(int));
    if (!start || !len) exit(ENOMEM);

    int m = 0;
    for (int i = 1; i < argc; ) {
        start[m] = i;
        len[m]   = 1;
        ++i;
        while (i < argc && argv[i][0] == '-') { ++len[m]; ++i; }
        ++m;
    }

    pid_t *pids = malloc(m * sizeof(pid_t));
    if (!pids) exit(ENOMEM);

    int prev = STDIN_FILENO;
    for (int i = 0; i < m; ++i) {
        int fds[2];
        if (i != m - 1 && pipe(fds) == -1) exit(errno);

        pid_t pid = fork();
        if (pid == -1) exit(errno);

        if (pid == 0) {
            if (prev != STDIN_FILENO && dup2(prev, STDIN_FILENO) == -1) _exit(errno);
            if (i != m - 1 && dup2(fds[1], STDOUT_FILENO) == -1)       _exit(errno);
            if (prev != STDIN_FILENO) close(prev);
            if (i != m - 1) { close(fds[0]); close(fds[1]); }

            char **stage = malloc((len[i] + 1) * sizeof(char *));
            if (!stage) _exit(ENOMEM);
            for (int k = 0; k < len[i]; ++k) stage[k] = argv[start[i] + k];
            stage[len[i]] = NULL;
            execvp(stage[0], stage);
            perror(stage[0]);
            _exit(errno == ENOENT ? 127 : 126);
        }

        pids[i] = pid;
        if (prev != STDIN_FILENO) close(prev);
        if (i != m - 1) { close(fds[1]); prev = fds[0]; }
    }

    int rc = 0, left = m;
    while (left) {
        int st; pid_t w = wait(&st);
        if (w == -1) { if (errno == EINTR) continue; exit(errno); }
        int translated = map_status(st);
        if (rc == 0 && translated) rc = translated;
        --left;
    }

    return rc;
}
