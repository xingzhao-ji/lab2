#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static int map_status(int s) {
    if (WIFSIGNALED(s)) return WTERMSIG(s) == SIGPIPE ? 0 : 128 + WTERMSIG(s);
    int c = WEXITSTATUS(s);
    return c == 128 + SIGPIPE ? 0 : c;
}

static int is_path_exec(const char *p) { return access(p, X_OK) == 0; }

static int looks_like_cmd(const char *t) {
    if (!t || !*t) return 0;
    if (strchr(t, '/')) return is_path_exec(t);
    const char *path = getenv("PATH");
    if (!path) return 0;
    char buf[4096];
    const char *seg = path;
    while (*seg) {
        const char *colon = strchr(seg, ':');
        size_t n = colon ? (size_t)(colon - seg) : strlen(seg);
        if (n + 1 + strlen(t) + 1 < sizeof(buf)) {
            memcpy(buf, seg, n);
            buf[n] = '/';
            strcpy(buf + n + 1, t);
            if (is_path_exec(buf)) return 1;
        }
        if (!colon) break;
        seg = colon + 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) exit(EINVAL);

    int max = argc - 1;
    int *start = malloc(max * sizeof(int));
    int *len   = malloc(max * sizeof(int));
    if (!start || !len) exit(ENOMEM);

    int m = 0, i = 1;
    while (i < argc) {
        start[m] = i;
        len[m]   = 1;
        ++i;
        while (i < argc && !looks_like_cmd(argv[i])) { ++len[m]; ++i; }
        ++m;
    }

    pid_t *pids = malloc(m * sizeof(pid_t));
    if (!pids) exit(ENOMEM);

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

    int rc = 0, left = m;
    while (left) {
        int st; pid_t w = wait(&st);
        if (w == -1) { if (errno == EINTR) continue; exit(errno); }
        int r = map_status(st);
        if (rc == 0 && r) rc = r;
        --left;
    }
    return rc;
}
