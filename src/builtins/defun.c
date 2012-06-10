#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_defun(interpreter*) */
/* @builtin-bind { 'd', builtin_defun }, */
int builtin_defun(interpreter* interp) {
  string name, body;
  byte n1;
  long_command* curr;

  if (!stack_pop_strings(interp, 2, &body, &name)) UNDERFLOW;

  /* If name is one character, it is a short name; if it is more than one
   * character, it is a long name. An empty string is an error.
   */
  if (!name->len) {
    print_error("Empty command name");
    goto error;
  }

  if (name->len == 1) {
    n1 = string_data(name)[0];
    /* Both pointers are in the same spot, so check either */
    if (interp->commands[n1].cmd.native) {
      print_error_s("Short command already exists", name);
      goto error;
    } else {
      /* OK */
      interp->commands[n1].is_native = 0;
      interp->commands[n1].cmd.user = body;
      free(name);
    }
  } else {
    /* Long command name.
     * First, check to see if it already exists.
     */
    for (curr = interp->long_commands; curr; curr = curr->next) {
      if (string_equals(name, curr->name)) {
        print_error_s("Long command already exists", name);
        goto error;
      }
    }

    /* OK, add it */
    curr = tmalloc(sizeof(long_command));
    curr->name = name;
    curr->cmd.is_native = 0;
    curr->cmd.cmd.user = body;
    curr->next = interp->long_commands;
    interp->long_commands = curr;
  }

  return 1;

  error:
  /* Restore the stack and return failure */
  stack_push(interp, name);
  stack_push(interp, body);
  return 0;
}

/* @builtin-decl int builtin_contextualdefun(interpreter*) */
/* @builtin-bind { 'D', builtin_contextualdefun }, */
int builtin_contextualdefun(interpreter* interp) {
  string a, b;
  if (interp->context_active)
    return builtin_defun(interp);
  else {
    /* Pop unused strings */
    if (!stack_pop_strings(interp, 2, &a, &b)) UNDERFLOW;
    free(a);
    free(b);
    return 1;
  }
}

/* Common function for v and V.
 *
 * aux: if non-NULL, any code to evaluate before the defun
 * defun: command to run to define the command.
 */
static int builtin_defunlibrary_common(interpreter* interp,
                                       string aux,
                                       byte defun) {
  string name, body, code;
  struct tm* now;
  time_t seconds;
  char header[256], date[64];
  FILE* out;
  unsigned i;

  if (!stack_pop_strings(interp, 2, &body, &name)) UNDERFLOW;

  /* We can't allow the name to have parentheses or the NUL character. */
  for (i = 0; i < name->len; ++i)
    if (string_data(name)[i] == '(' || string_data(name)[i] == ')' ||
        string_data(name)[i] == 0) {
      print_error_s("Invalid command name (for use with v/V)", name);
      stack_push(interp, name);
      stack_push(interp, body);
      return 0;
    }

  /* Build code */
  time(&seconds);
  now = localtime(&seconds);
  strftime(date, sizeof(date)-1, "%A, %Y.%m.%d %H:%M:%S", now);
  snprintf(header, sizeof(header), "\n(Added by %s on %s);\n",
           getenv("USER"), date);
  code = convert_string(header);
  if (aux)
    code = append_string(code, aux);
  code = append_cstr(code, "(");
  code = append_string(code, name);
  code = append_cstr(code, ")(");
  code = append_string(code, body);
  code = append_cstr(code, ")");
  code = append_data(code, &defun, (&defun)+1);
  code = append_cstr(code, "\n");

  /* Evaluate in current */
  if (!exec_code(interp, code)) {
    print_error("not adding function to library due to error(s)");
    stack_push(interp, name);
    stack_push(interp, code);
    return 0;
  }

  /* Don't need name or code anymore. */
  free(name);
  free(body);

  /* OK, write out */
  out = fopen(user_library_file, "a");
  if (!out) {
    fprintf(stderr, "tgl: error: unable to open %s: %s\n",
            user_library_file, strerror(errno));
    free(code);
    return 0;
  }
  if (code->len !=
      fwrite(string_data(code), 1, code->len, out)) {
    fprintf(stderr, "tgl: error writing to %s: %s\n",
            user_library_file, strerror(errno));
    free(code);
    fclose(out);
    return 0;
  }

  /* Success */
  fclose(out);
  free(code);
  return 1;
}

/* @builtin-decl int builtin_defunlibrary(interpreter*) */
/* @builtin-bind { 'v', builtin_defunlibrary }, */
int builtin_defunlibrary(interpreter* interp) {
  return builtin_defunlibrary_common(interp, NULL, 'd');
}

/* @builtin-decl int builtin_contextualdefunlibrary(interpreter*) */
/* @builtin-bind { 'V', builtin_contextualdefunlibrary }, */
int builtin_contextualdefunlibrary(interpreter* interp) {
  int status;
  string cxt;

  /* Get subcommand */
  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Expected subcommand");
    return 0;
  }

  switch (curr(interp)) {
  case 's':
    cxt = convert_string("@=");
    cxt = append_cstr(cxt, current_context);
    cxt = append_cstr(cxt, "\n");
    break;

  case 'e':
    cxt = convert_string("@=");
    cxt = append_cstr(cxt, get_context_extension(current_context));
    cxt = append_cstr(cxt, "\n");
    break;

  default:
    print_error("Unknown subcommand");
    return 0;
  }

  status = builtin_defunlibrary_common(interp, cxt, 'D');
  free(cxt);
  return status;
}
