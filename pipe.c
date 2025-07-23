#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

static int map_status(int s) {
    if (WIFSIGNALED(s)) {
        int sg = WTERMSIG(s);
        if (sg == SIGPIPE) {
            return 0;
        } else {
            return 128 + sg;
        }
    }

    int c = WEXITSTATUS(s);
    if (c == 128 + SIGPIPE) {
        return 0;
    } else {
        return c;
    }
}

static int looks_cmd(const char *t) {
    if (t == NULL || *t == '\0') {
        return 0;
    }

    if (strlen(t) == 1) {
        return 0;
    }

    if (t[0] == '-' || isdigit((unsigned char)t[0])) {
        return 0;
    }

    for (const char *p = t; *p != '\0'; ++p) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '/') {
            return 0;
        }
    }

    return 1;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        errno = EINVAL;
        perror("pipe");
        exit(errno);
    }

    int max = argc - 1;
    int *start = malloc(max * sizeof(int));
    int *len   = malloc(max * sizeof(int));
    if (start == NULL || len == NULL) {
        exit(ENOMEM);
    }

    int m = 0;
    int i = 1;
    while (i < argc) {
        start[m] = i;
        len[m] = 1;
        ++i;
        while (i < argc && !looks_cmd(argv[i])) {
            ++len[m];
            ++i;
        }
        ++m;
    }

    pid_t *pids  = malloc(m * sizeof(pid_t));
    int   *codes = malloc(m * sizeof(int));
    if (pids == NULL || codes == NULL) {
        exit(ENOMEM);
    }
    for (i = 0; i < m; ++i) {
        codes[i] = -1;
    }

    int prev_rd = STDIN_FILENO;
    for (i = 0; i < m; ++i) {
        int fds[2] = { -1, -1 };
        if (i != m - 1) {
            if (pipe(fds) == -1) {
                perror("pipe");
                exit(errno);
            }
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(errno);
        }

        if (pid == 0) {                        /* child */
            if (prev_rd != STDIN_FILENO) {
                if (dup2(prev_rd, STDIN_FILENO) == -1) {
                    _exit(errno);
                }
            }
            if (i != m - 1) {
                if (dup2(fds[1], STDOUT_FILENO) == -1) {
                    _exit(errno);
                }
            }
            if (prev_rd != STDIN_FILENO) {
                close(prev_rd);
            }
            if (i != m - 1) {
                close(fds[0]);
                close(fds[1]);
            }

            char **stage = malloc((len[i] + 1) * sizeof(char *));
            if (stage == NULL) {
                _exit(ENOMEM);
            }
            for (int k = 0; k < len[i]; ++k) {
                stage[k] = argv[start[i] + k];
            }
            stage[len[i]] = NULL;

            execvp(stage[0], stage);
            perror(stage[0]);
            _exit(errno);
        }

        pids[i] = pid;
        if (prev_rd != STDIN_FILENO) {
            close(prev_rd);
        }
        if (i != m - 1) {
            close(fds[1]);
            prev_rd = fds[0];
        }
    }

    for (int left = m; left > 0; ) {
        int st;
        pid_t w = wait(&st);
        if (w == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("wait");
            exit(errno);
        }
        for (int k = 0; k < m; ++k) {
            if (pids[k] == w && codes[k] == -1) {
                codes[k] = map_status(st);
                break;
            }
        }
        --left;
    }

    int rc = 0;
    for (i = 0; i < m; ++i) {
        if (codes[i] != 0) {
            rc = codes[i];
            break;
        }
    }

    return rc;
}
