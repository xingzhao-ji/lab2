#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static int map(int s) {
    if (WIFSIGNALED(s)) return WTERMSIG(s) == SIGPIPE ? 0 : 128 + WTERMSIG(s);
    int c = WEXITSTATUS(s);
    return c == 128 + SIGPIPE ? 0 : c;
}

static int is_num(const char *t) {
    if (!t || !*t) return 0;
    for (const char *p = t; *p; ++p) if (*p < '0' || *p > '9') return 0;
    return 1;
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
        while (i < argc && (argv[i][0] == '-' || is_num(argv[i]))) { ++len[m]; ++i; }
        ++m;
    }

    pid_t *pids  = malloc(m * sizeof(pid_t));
    int   *codes = malloc(m * sizeof(int));
    if (!pids || !codes) exit(ENOMEM);
    for (int i = 0; i < m; ++i) codes[i] = -1;

    int prev = STDIN_FILENO;
    for (int k = 0; k < m; ++k) {
        int fds[2];
        if (k != m - 1 && pipe(fds) == -1) exit(errno);

        pid_t pid = fork();
        if (pid == -1) exit(errno);

        if (pid == 0) {
            if (prev != STDIN_FILENO && dup2(prev, STDIN_FILENO) == -1) _exit(errno);
            if (k != m - 1 && dup2(fds[1], STDOUT_FILENO) == -1)       _exit(errno);
            if (prev != STDIN_FILENO) close(prev);
            if (k != m - 1) { close(fds[0]); close(fds[1]); }

            char **stage = malloc((len[k] + 1) * sizeof(char *));
            if (!stage) _exit(ENOMEM);
            for (int j = 0; j < len[k]; ++j) stage[j] = argv[start[k] + j];
            stage[len[k]] = NULL;

            execvp(stage[0], stage);
            perror(stage[0]);
            _exit(errno);
        }

        pids[k] = pid;
        if (prev != STDIN_FILENO) close(prev);
        if (k != m - 1) { close(fds[1]); prev = fds[0]; }
    }

    for (int left = m; left; ) {
        int st; pid_t w = wait(&st);
        if (w == -1) { if (errno == EINTR) continue; exit(errno); }
        for (int i = 0; i < m; ++i)
            if (pids[i] == w && codes[i] == -1) { codes[i] = map(st); break; }
        --left;
    }

    int rc = 0;
    for (int i = 0; i < m; ++i) if (codes[i]) { rc = codes[i]; break; }

    return rc;
}
