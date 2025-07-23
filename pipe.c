// #include <unistd.h>
// #include <sys/types.h>
// #include <sys/wait.h>
// #include <stdlib.h>
// #include <errno.h>
// #include <signal.h>
// #include <stdio.h>
// #include <ctype.h>
// #include <string.h>

// static int map_status(int s) {
//     if (WIFSIGNALED(s)) {
//         int sg = WTERMSIG(s);
//         return sg == SIGPIPE ? 0 : 128 + sg;
//     }
//     int c = WEXITSTATUS(s);
//     return c == 128 + SIGPIPE ? 0 : c;
// }

// static int looks_cmd(const char *t) {
//     if (!t || !*t) return 0;
//     if (strlen(t) == 1) return 0;                 /* single‑char token ⇒ arg */
//     if (t[0] == '-' || isdigit((unsigned char)t[0])) return 0;
//     for (const char *p = t; *p; ++p)
//         if (!isalnum((unsigned char)*p) && *p != '_' && *p != '/')
//             return 0;
//     return 1;
// }

// int main(int argc, char *argv[]) {
//     if (argc <= 1) { errno = EINVAL; perror("pipe"); exit(errno); }

//     int max = argc - 1;
//     int *start = malloc(max * sizeof(int));
//     int *len   = malloc(max * sizeof(int));
//     if (!start || !len) exit(ENOMEM);

//     int m = 0, i = 1;
//     while (i < argc) {
//         start[m] = i; len[m] = 1; ++i;
//         while (i < argc && !looks_cmd(argv[i])) { ++len[m]; ++i; }
//         ++m;
//     }

//     pid_t *pids  = malloc(m * sizeof(pid_t));
//     int   *codes = malloc(m * sizeof(int));
//     if (!pids || !codes) exit(ENOMEM);
//     for (i = 0; i < m; ++i) codes[i] = -1;

//     int prev_rd = STDIN_FILENO;
//     for (i = 0; i < m; ++i) {
//         int fds[2] = {-1, -1};
//         if (i != m - 1 && pipe(fds) == -1) { perror("pipe"); exit(errno); }

//         pid_t pid = fork();
//         if (pid == -1) { perror("fork"); exit(errno); }

//         if (pid == 0) {
//             if (prev_rd != STDIN_FILENO && dup2(prev_rd, STDIN_FILENO) == -1) _exit(errno);
//             if (i != m - 1 && dup2(fds[1], STDOUT_FILENO) == -1)              _exit(errno);
//             if (prev_rd != STDIN_FILENO) { close(prev_rd); }
//             if (i != m - 1) { close(fds[0]); close(fds[1]); }

//             char **stage = malloc((len[i] + 1) * sizeof(char *));
//             if (!stage) _exit(ENOMEM);
//             for (int k = 0; k < len[i]; ++k) stage[k] = argv[start[i] + k];
//             stage[len[i]] = NULL;
//             execvp(stage[0], stage);
//             perror(stage[0]);
//             _exit(errno);
//         }

//         pids[i] = pid;
//         if (prev_rd != STDIN_FILENO) close(prev_rd);
//         if (i != m - 1) { close(fds[1]); prev_rd = fds[0]; }
//     }

//     for (int left = m; left; ) {
//         int st; pid_t w = wait(&st);
//         if (w == -1) { if (errno == EINTR) continue; perror("wait"); exit(errno); }
//         for (int k = 0; k < m; ++k)
//             if (pids[k] == w && codes[k] == -1) { codes[k] = map_status(st); break; }
//         --left;
//     }

//     int rc = 0;
//     for (i = 0; i < m; ++i) if (codes[i]) { rc = codes[i]; break; }
//     return rc;
// }
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

// NOTE: This an implementation of the strategy explained in discussion!!!

int main(int argc, char *argv[])
{
	// 0 Arguments: exit early
	if (argc <= 1) {
		errno = EINVAL;
		perror("ERROR: No arguments provided");
		exit(errno);
	}

	// 1 Argument: execute normally (avoid making unnecessary pipe)
	if (argc == 2) {
		if (execlp(argv[1], argv[1], NULL) == -1) {
			perror("ERROR: Failed to execute the single program inputted.");
			exit(errno);
		}
		return 0;
	}

	// 2+ Arguments (pipe time!)
	if (argc >= 3) {
		// Multiple pipes to handle more than two arguments (more space efficient than a different pipe for every program)
		int curr_pipe[2];
		int prev_pipe[2];

		for (int i = 1; i < argc; i++) {
			// Last argument = do not create a pipe
			if (i < argc - 1)
				pipe(curr_pipe);

			// Create child process
			int pid = fork();

			if (pid < 0) {
				perror("ERROR: Fork failed!");
				exit(errno);
			}

			// Child process
			if (pid == 0) {
				// Not the first program - redirect the input from previous
				if (i > 1) {
					dup2(prev_pipe[0], 0);
					close(prev_pipe[0]);
				}

				// Not the last program - redirect the output into curr
				if (i < argc - 1) {
					dup2(curr_pipe[1], 1);
					close(curr_pipe[1]);
				}

				// Cleanup - after establishing previous fd modifications, we don't need these anymore
				close(curr_pipe[0]);
				close(curr_pipe[1]);

				if (execlp(argv[i], argv[i], NULL) == -1) {
					perror("ERROR: Failed to execute program from within child process");
					exit(errno);
				}
				
				return 0;
			} else { // Parent process
				// Wait for child process to finish executing
				int status;
				wait(&status);
				if (WEXITSTATUS(status) != 0) {
					// This should handle if dup2 or close give an error...
					perror("ERROR: Failed executing a child process, most likely related to the pipe!");
					return WEXITSTATUS(status);
				}

				// The write fd is unnecessary; we only need the read if we have another pipe
				// The read fd can stay open! Writing acts like a stream so we must close it else the program will hang
				close(curr_pipe[1]);

				// Not the last program - store old pipe to make space for new pipe
				if (i < (argc - 1)) {
					prev_pipe[0] = curr_pipe[0];
					prev_pipe[1] = curr_pipe[1];
				} else {
					// Last program, close all pipes for cleanup just in case...
					close(prev_pipe[0]);
					close(prev_pipe[1]);
					close(curr_pipe[0]);
					close(curr_pipe[1]);
				}
			}
		}
	}

	return 0;
}
