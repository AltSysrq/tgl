#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* Invokes the specified command and arguments, all verbatim.
 *
 * If input is non-NULL, it's contents are piped into the child process's
 * standard input. Otherwise, the child process receives no input.
 *
 * The process's standard output is captured and accumulated into a
 * string. Standard error is inherited from Tgl.
 *
 * On success, the standard output of the process is returned. On error, a
 * message is printed to standard error and NULL is returned.
 */
static string invoke_external(char** argv, string input) {
  FILE* output_file = NULL, * input_file = NULL;
  int output_fd, input_fd;
  string output;
  long output_length;
  unsigned output_off;
  pid_t child;
  int child_status;

  /* I believe the most portable way to do this is to use tmpfile() to open two
   * temporary files, one for input and one for output. Write the input to that
   * file, then execute the child, then read its output back in once it dies.
   *
   * Of course, tmpfile() is broken on Windows, but then Windows doesn't have
   * fork() either, so regardless this function must be rewritten for that
   * platform.
   */
  input_file = tmpfile();
  if (!input_file) {
    fprintf(stderr, "tgl: error: creating temp input file: %s\n",
            strerror(errno));
    goto error;
  }

  output_file = tmpfile();
  if (!output_file) {
    fprintf(stderr, "tgl: error: creating temp output file: %s\n",
            strerror(errno));
    goto error;
  }

  if ((input_fd  = fileno(input_file )) == -1 ||
      (output_fd = fileno(output_file)) == -1) {
    fprintf(stderr, "tgl: unexpected error: %s\n", strerror(errno));
    goto error;
  }

  /* File descriptors set up; now write input */
  if (input && input->len) {
    if (!fwrite(string_data(input), 1, input->len, input_file)) {
      fprintf(stderr, "tgl: error: writing command input: %s\n",
              strerror(errno));
      goto error;
    }
    rewind(input_file);
  }

  /* Execute the child */
  child = fork();
  if (child == -1) {
    fprintf(stderr, "tgl: error: fork: %s\n", strerror(errno));
    goto error;
  }

  if (!child) {
    /* Child process */
    if (-1 == dup2(input_fd, STDIN_FILENO) ||
        -1 == dup2(output_fd, STDOUT_FILENO)) {
      fprintf(stderr, "tgl: error: dup2: %s\n", strerror(errno));
      exit(EXIT_IO_ERROR);
    }
    /* Close original FDs */
    close(input_fd);
    close(output_fd);

    /* Switch to new process */
    execvp(argv[0], argv);
    /* If we get here, execvp() failed */
    fprintf(stderr, "tgl: error: execvp: %s\n", strerror(errno));
    exit(EXIT_PLATFORM_ERROR);
  }

  /* Parent process. Wait for child to die. */
  if (-1 == waitpid(child, &child_status, 0)) {
    fprintf(stderr, "tgl: error: waitpid: %s\n", strerror(errno));
    goto error;
  }

  /* Did the child exit successfully? */
  if (!WIFEXITED(child_status)) {
    fprintf(stderr, "tgl: error: child process %s terminated abnormally\n",
            argv[0]);
    goto error;
  }
  if (WEXITSTATUS(child_status)) {
    fprintf(stderr, "tgl: error: child process %s exited with code %d\n",
            argv[0], (int)WEXITSTATUS(child_status));
    goto error;
  }

  /* OK, get output */
  output_length = lseek(output_fd, 0, SEEK_END);
  if (output_length == -1) {
    fprintf(stderr, "tgl: error: lseek: %s\n", strerror(errno));
    goto error;
  }
  output = tmalloc(sizeof(struct string) + output_length);
  output->len = output_length;
  for (output_off = 0; output_off < output->len && output_length >= 1;
       output_off += output_length)
    output_length = read(output_fd, string_data(output) + output_off,
                         output->len - output_off);

  if (output_length == -1) {
    fprintf(stderr, "tgl: error: read: %s\n", strerror(errno));
    free(output);
    goto error;
  }
  if (output_length == 0 && output_off < output->len) {
    fprintf(stderr, "tgl: error: EOF in encountered earlier than expected\n");
    fprintf(stderr, "tgl: This is likely a bug.\n");
    fprintf(stderr, "tgl: %s:%d\n", __FILE__, __LINE__);
    free(output);
    goto error;
  }

  /* Done */
  fclose(input_file);
  fclose(output_file);
  return output;

  error:
  if (input_file) fclose(input_file);
  if (output_file) fclose(output_file);
  return NULL;
}
