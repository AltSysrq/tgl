#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_long_command(interpreter*) */
/* @builtin-bind { 'Q', builtin_long_command }, */
int builtin_long_command(interpreter* interp) {
  unsigned begin, end;
  string commandName;
  long_command* curr;
  int result;

  /* Extract the long name */
  begin = ++interp->ip;
  while (interp->ip < interp->code->len &&
         !isspace(string_data(interp->code)[interp->ip]))
    ++interp->ip;

  end = interp->ip;
  if (begin == end || begin+1 == end) {
    print_error("Long command name expected");
    return 0;
  }

  commandName = create_string(string_data(interp->code)+begin,
                              string_data(interp->code)+end);

  /* Find the first long command name which matches. */
  curr = interp->long_commands;
  while (curr && !string_equals(curr->name, commandName))
    curr = curr->next;

  /* Don't need the command name anymore. */
  free(commandName);

  if (!curr) {
    print_error("Long command not found");
    return 0;
  }

  /* Execute, clean up, and return */
  if (curr->cmd.is_native)
    result = curr->cmd.cmd.native(interp);
  else
    result = exec_code(interp, curr->cmd.cmd.user);

  return result;
}
