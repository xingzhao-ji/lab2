#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

int main(int argc, char *argv[]) {
    if (argc <= 1) exit(EINVAL);

    int n = argc - 1;
    pid_t *pids = malloc((size_t)n * sizeof(pid_t));
    if (!pids) exit(ENOMEM);
    int *codes = malloc((size_t)n * sizeof(int));
    if (!codes) { free(pids); exit(ENOMEM); }
    for (int i = 0; i < n; ++i) codes[i] = -1;

    int prev = STDIN_FILENO;
    for (int i = 0; i < n; ++i) {
        int fds[2];
        if (i != n - 1 && pipe(fds) == -1) { int e = errno; free(pids); free(codes); exit(e); }

        pid_t pid = fork();
        if (pid == -1) {
            int e = errno;
            if (i != n - 1) { close(fds[0]); close(fds[1]); }
            free(pids); free(codes); exit(e);
        }

        if (pid == 0) {
            if (prev != STDIN_FILENO && dup2(prev, STDIN_FILENO) == -1) _exit(errno);
            if (i != n - 1 && dup2(fds[1], STDOUT_FILENO) == -1) _exit(errno);
            if (prev != STDIN_FILENO) close(prev);
            if (i != n - 1) { close(fds[0]); close(fds[1]); }
            execlp(argv[i + 1], argv[i + 1], (char *)NULL);
            _exit(errno);
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
            int e = errno; free(pids); free(codes); exit(e);
        }
        int idx = -1;
        for (int i = 0; i < n; ++i) if (pids[i] == w) { idx = i; break; }
        if (idx == -1) continue;

        int code = 0;
        if (WIFEXITED(status)) code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            code = (sig == SIGPIPE) ? 0 : 128 + sig;
        }
        codes[idx] = code;
        --remaining;
    }

    int rc = 0;
    for (int i = 0; i < n; ++i) {
        if (codes[i] != 0) { rc = codes[i]; break; }
    }

    free(pids);
    free(codes);
    return rc;
}
