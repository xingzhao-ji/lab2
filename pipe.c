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
