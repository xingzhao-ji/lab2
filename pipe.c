#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>

static int map_status(int status) {
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        return sig == SIGPIPE ? 0 : 128 + sig;
    }
    int code = WEXITSTATUS(status);
    if (code == 128 + SIGPIPE) return 0;
    return code;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) exit(EINVAL);

    int n = argc - 1;
    pid_t *pids = malloc((size_t)n * sizeof(pid_t));
    int *codes   = malloc((size_t)n * sizeof(int));
    if (!pids || !codes) exit(ENOMEM);
    for (int i = 0; i < n; ++i) codes[i] = -1;

    int prev = STDIN_FILENO;

    for (int i = 0; i < n; ++i) {
        int fds[2];
        if (i != n - 1 && pipe(fds) == -1) exit(errno);

        pid_t pid = fork();
        if (pid == -1) exit(errno);

        if (pid == 0) {
            if (prev != STDIN_FILENO && dup2(prev, STDIN_FILENO) == -1) _exit(errno);
            if (i != n - 1 && dup2(fds[1], STDOUT_FILENO) == -1) _exit(errno);
            if (prev != STDIN_FILENO) close(prev);
            if (i != n - 1) { close(fds[0]); close(fds[1]); }
            execlp(argv[i + 1], argv[i + 1], (char *)0);
            perror(argv[i + 1]);
            _exit(errno == ENOENT ? 127 : 126);
        }

        pids[i] = pid;
        if (prev != STDIN_FILENO) close(prev);
        if (i != n - 1) {
            close(fds[1]);
            prev = fds[0];
        }
    }

    int remaining = n;
    while (remaining) {
        int status;
        pid_t w = wait(&status);
        if (w == -1) {
            if (errno == EINTR) continue;
            exit(errno);
        }
        int idx = -1;
        for (int i = 0; i < n; ++i) if (pids[i] == w) { idx = i; break; }
        if (idx == -1 || codes[idx] != -1) continue;
        codes[idx] = map_status(status);
        --remaining;
    }

    int rc = 0;
    for (int i = 0; i < n; ++i) if (codes[i]) { rc = codes[i]; break; }

    free(pids);
    free(codes);
    return rc;
}
