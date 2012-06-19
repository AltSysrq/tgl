#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>

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
 * If return_status is non-NULL, write the exit status of the child there
 * instead of considering a non-zero exit status to be an error. Abnormal
 * termination still constitutes an error.
 *
 * On success, the standard output of the process is returned. On error, a
 * message is printed to standard error and NULL is returned.
 */
static string invoke_external(char** argv, string input, int* return_status) {
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
  if (return_status) {
    *return_status = WEXITSTATUS(child_status);
  } else if (WEXITSTATUS(child_status)) {
    fprintf(stderr, "tgl: error: child process %s exited with code %d\n",
            argv[0], (int)WEXITSTATUS(child_status));
    goto error;
  }

  /* OK, get output */
  output_length = lseek(output_fd, 0, SEEK_END);
  if (output_length == -1 || -1 == lseek(output_fd, 0, SEEK_SET)) {
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

/* @builtin-decl int builtin_shell_script(interpreter*) */
/* @builtin-bind { 'b', builtin_shell_script }, */
int builtin_shell_script(interpreter* interp) {
  string input, sscript, output;
  char* script, *argv[4];
  byte status_reg;
  signed status_reg_value, * status_reg_ptr = NULL;

  /* If a status register is provided, get the register and set the pointer
   * indicating we want the status returned.
   */
  if (!secondary_arg_as_reg(interp->u[0], &status_reg))
    return 0;
  if (interp->u[0]) status_reg_ptr = &status_reg_value;

  if (!getenv("SHELL")) {
    print_error("$SHELL undefined");
    return 0;
  }
  if (!stack_pop_strings(interp, 2, &sscript, &input)) UNDERFLOW;

  /* Copy script to NTBS */
  script = string_to_cstr(sscript);

  /* Set arguments up */
  argv[0] = getenv("SHELL");
  argv[1] = "-c";
  argv[2] = script;
  argv[3] = NULL;
  output = invoke_external(argv, input, status_reg_ptr);
  free(script);

  /* If unsuccessful, restore the stack and we're done. */
  if (!output) {
    stack_push(interp, input);
    stack_push(interp, sscript);
    return 0;
  }

  /* OK, free the inputs and return success.
   * First, set the status register if requested.
   */
  if (status_reg_ptr) {
    free(interp->registers[status_reg]);
    interp->registers[status_reg] = int_to_string(status_reg_value);
    touch_reg(interp, status_reg);
  }
  free(input);
  free(sscript);
  stack_push(interp, output);
  reset_secondary_args(interp);
  return 1;
}

/* @builtin-decl int builtin_shell_command(interpreter*) */
/* @builtin-bind { 'B', builtin_shell_command }, */
int builtin_shell_command(interpreter* interp) {
  string* sargv=NULL, input=NULL, output, sargc=NULL;
  char** argv;
  signed argc;
  unsigned i, stack_height;
  stack_elt* elt;
  byte status_reg;
  signed status_reg_value, * status_reg_ptr = NULL;

  /* If a status register is provided, get the register and set the pointer
   * indicating we want the status returned.
   */
  if (!secondary_arg_as_reg(interp->u[1], &status_reg))
    return 0;
  if (interp->u[1]) status_reg_ptr = &status_reg_value;

  if (interp->u[0]) {
    if (!secondary_arg_as_int(interp->u[0], &argc, 0)) return 0;

    stack_height = 0;
    for (elt = interp->stack; elt; elt = elt->next)
      ++stack_height;

    if (argc >= stack_height) {
      print_error("Invalid secondary argument");
      return 0;
    }

    argc = stack_height - argc;

    if (!argc) {
      print_error("Empty shell command");
      goto error;
    }
  } else {
    if (!(sargc = stack_pop(interp))) UNDERFLOW;
    if (!string_to_int(sargc, &argc)) {
      print_error_s("Invalid integer", sargc);
      goto error;
    }
    if (argc <= 0 || argc >= 4096) {
      print_error_s("Invalid number of arguments", sargc);
      goto error;
    }
  }

  sargv = tmalloc(sizeof(string*) * argc);
  if (!stack_pop_array(interp, argc, sargv)) {
    print_error("Stack underflow");
    free(sargv);
    sargv = NULL;
    goto error;
  }

  if (!(input = stack_pop(interp))) {
    print_error("Stack underflow");
    goto error;
  }

  /* Got all parms correctly, set argument vector up.
   * Since the args were popped from right to left, the order must be reversed
   * here.
   */
  argv = tmalloc(sizeof(char*) * (argc+1));
  for (i = 0; i < argc; ++i)
    argv[argc-i-1] = string_to_cstr(sargv[i]);
  argv[argc] = NULL;

  /* Run the command, then immediately free memory before checking for error. */
  output = invoke_external(argv, input, status_reg_ptr);
  for (i = 0; i < argc; ++i)
    free(argv[i]);
  free(argv);

  if (!output) goto error;

  /* OK, clean up and return success */
  if (status_reg_ptr) {
    free(interp->registers[status_reg]);
    interp->registers[status_reg] = int_to_string(status_reg_value);
    touch_reg(interp, status_reg);
  }
  for (i = 0; i < argc; ++i)
    free(sargv[i]);
  free(sargv);
  if (sargc)
    free(sargc);
  free(input);
  stack_push(interp, output);
  reset_secondary_args(interp);
  return 1;

  error:
  /* Restore stack, free memory, and return failure */
  if (input)
    stack_push(interp, input);
  if (sargv) {
    for (i = 0; i < argc; ++i)
      stack_push(interp, sargv[i]);
    free(sargv);
  }
  if (sargc)
    stack_push(interp, sargc);
  return 0;
}

/* @builtin-decl int builtin_sed(interpreter*) */
/* @builtin-bind { 'j', builtin_sed }, */
int builtin_sed(interpreter* interp) {
  string input, output, sscript=NULL;
  char* script, *argv[4];
  unsigned begin;
  int has_seen_middle_delim;
  byte delim;

  /* Read the script in */
  begin = interp->ip + 1;
  do {
    /* Advance past r or ; */
    ++interp->ip;
    if (!is_ip_valid(interp) || !isalpha(curr(interp))) break;
    ++interp->ip;
    if (!is_ip_valid(interp)) {
      /* The previous character wasn't part of the script */
      --interp->ip;
      break;
    }

    delim = curr(interp);
    ++interp->ip;
    has_seen_middle_delim = 0;

    /* Read to the end of the script, other than flags */
    while (is_ip_valid(interp) &&
           (!has_seen_middle_delim || curr(interp) != delim)) {
      if (curr(interp) == delim) has_seen_middle_delim = 1;
      ++interp->ip;
    }

    if (!is_ip_valid(interp)) {
      print_error("sed script runs past end of input");
      return 0;
    }

    /* Advance past closing delimiter */
    ++interp->ip;
    /* Read flags */
    while (is_ip_valid(interp) && isalpha(curr(interp)))
      ++interp->ip;
  } while (is_ip_valid(interp) && curr(interp) == ';');

  /* Obtain input */
  if (interp->ip == begin) {
    if (!stack_pop_strings(interp, 2, &sscript, &input)) UNDERFLOW;
  } else {
    if (!(input = stack_pop(interp))) UNDERFLOW;
  }

  /* Extract the script */
  if (sscript) {
    script = string_to_cstr(sscript);
  } else {
    script = tmalloc(interp->ip - begin + 1);
    memcpy(script, string_data(interp->code) + begin, interp->ip - begin);
    script[interp->ip - begin] = 0;
  }

  /* Move the IP back one since it must be one character before the next
   * command to execute.
   */
  --interp->ip;

  /* Set argument vector up */
  argv[0] = (getenv("TGL_SED")? getenv("TGL_SED") : "sed");
  argv[1] = "-r";
  argv[2] = script;
  argv[3] = 0;

  /* Execute and clean up */
  output = invoke_external(argv, input, NULL);
  free(script);

  /* On error, restore stack and return failure */
  if (!output) {
    if (sscript)
      stack_push(interp, sscript);
    stack_push(interp, input);
    return 0;
  }

  /* Otherwise, free input and return success */
  stack_push(interp, output);
  free(input);
  if (sscript)
    free(sscript);
  return 1;
}

/* @builtin-decl int builtin_perl(interpreter*) */
/* @builtin-bind { 'J', builtin_perl }, */
int builtin_perl(interpreter* interp) {
  string sscript, input, output;
  char* script, *argv[4];

  if (!stack_pop_strings(interp, 2, &sscript, &input)) UNDERFLOW;

  /* Set argument vector up */
  script = string_to_cstr(sscript);
  argv[0] = (getenv("TGL_PERL")? getenv("TGL_PERL") : "perl");
  argv[1] = "-E";
  argv[2] = script;
  argv[3] = NULL;

  /* Invoke and clean up */
  output = invoke_external(argv, input, NULL);
  free(script);

  if (!output) {
    stack_push(interp, input);
    stack_push(interp, sscript);
    return 0;
  }

  stack_push(interp, output);
  free(input);
  free(sscript);
  return 1;
}

/* @builtin-decl int builtin_tcl(interpreter*) */
/* @builtin-bind { 't', builtin_tcl }, */
int builtin_tcl(interpreter* interp) {
  int tempfile = -1;
  char tempname[] = "tgltclXXXXXX", *argv[3];
  string script, input, output;

  if (!stack_pop_strings(interp, 2, &script, &input)) UNDERFLOW;

  /* Tclsh is a bit odd in that it has no option to take its commands from the
   * command line. We would use its stdin, except that that is already used by
   * the user-supplied input. Therefore, write to a temporary file and invoke
   * tclsh on it.
   */
  tempfile = mkstemp(tempname);
  if (tempfile == -1) {
    fprintf(stderr, "tgl: error: mkstemp: %s\n", strerror(errno));
    goto error;
  }

  if (script->len != write(tempfile, string_data(script), script->len)) {
    fprintf(stderr, "tgl: error: writing Tcl script: %s\n", strerror(errno));
    goto error;
  }

  close(tempfile);
  tempfile = -1;

  /* Set argument vector up and invoke */
  argv[0] = (getenv("TGL_TCL")? getenv("TGL_TCL") : "tclsh");
  argv[1] = tempname;
  argv[2] = NULL;
  output = invoke_external(argv, input, NULL);

  if (unlink(tempname))
    fprintf(stderr, "tgl: warning: could not delete Tcl script %s: %s\n",
            tempname, strerror(errno));

  if (!output) goto error;

  free(script);
  free(input);
  stack_push(interp, output);
  return 1;

  error:
  stack_push(interp, input);
  stack_push(interp, script);
  if (tempfile != -1)
    close(tempfile);
  return 0;
}
