#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_history(interpreter*) */
/* @builtin-bind { 'h', builtin_history }, */
int builtin_history(interpreter* interp) {
  signed off = 0;

  if (interp->u[0])
    if (!secondary_arg_as_int(interp->u[0], &off, 1))
      return 0;

  off += interp->history_offset;

  if (off < 0 || off >= 0x20) {
    if (interp->u[0])
      print_error_s("Invalid history offset", interp->u[0]);
    else
      print_error("History exhausted");
    return 0;
  }

  /* OK */
  stack_push(interp, dupe_string(interp->registers[off]));
  reset_secondary_args(interp);
  ++interp->history_offset;
  return 1;
}

/* @builtin-decl int builtin_suppresshistory(interpreter*) */
/* @builtin-bind { 'H', builtin_suppresshistory }, */
int builtin_suppresshistory(interpreter* interp) {
  interp->enable_history = 0;
  return 1;
}
