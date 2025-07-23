#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    if (argc <= 1) exit(EINVAL);

    int n = argc - 1;
    pid_t *pids = malloc((size_t)n * sizeof(pid_t));
    if (!pids) exit(ENOMEM);

    int prev_read = STDIN_FILENO;

    for (int i = 0; i < n; ++i) {
        int fds[2];
        if (i != n - 1 && pipe(fds) == -1) {
            int e = errno;
            free(pids);
            exit(e);
        }

        pid_t pid = fork();
        if (pid == -1) {
            int e = errno;
            if (i != n - 1) {
                close(fds[0]);
                close(fds[1]);
            }
            free(pids);
            exit(e);
        }

        if (pid == 0) {
            if (prev_read != STDIN_FILENO) {
                if (dup2(prev_read, STDIN_FILENO) == -1) _exit(errno);
            }
            if (i != n - 1) {
                if (dup2(fds[1], STDOUT_FILENO) == -1) _exit(errno);
            }
            if (prev_read != STDIN_FILENO) close(prev_read);
            if (i != n - 1) {
                close(fds[0]);
                close(fds[1]);
            }
            execlp(argv[i + 1], argv[i + 1], (char *)NULL);
            _exit(errno);
        }

        pids[i] = pid;
        if (prev_read != STDIN_FILENO) close(prev_read);
        if (i != n - 1) {
            close(fds[1]);
            prev_read = fds[0];
        }
    }

    pid_t last_pid = pids[n - 1];
    int waited = 0, exit_code = 0, status;

    while (waited < n) {
        pid_t w = waitpid(-1, &status, 0);
        if (w == -1) {
            if (errno == EINTR) continue;
            free(pids);
            exit(errno);
        }
        ++waited;
        if (w == last_pid) {
            if (WIFEXITED(status))       exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);
        }
    }

    free(pids);
    return exit_code;
}
