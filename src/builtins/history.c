#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_history(interpreter*) */
/* @builtin-bind { 'h', builtin_history }, */
int builtin_history(interpreter* interp) {
  string soff;
  signed off;

  soff = stack_pop(interp);
  if (!soff)
    /* Assume 0 */
    off = 0;
  else
    if (!string_to_int(soff, &off)) {
      print_error("Invalid integer");
      stack_push(interp, soff);
      return 0;
    }

  off += interp->history_offset;

  if (off < 0 || off >= 0x20) {
    print_error("Invalid history offset");
    if (soff)
      stack_push(interp, soff);
    return 0;
  }

  /* OK */
  stack_push(interp, dupe_string(interp->registers[off]));
  free(soff);
  ++interp->history_offset;
  return 1;
}

/* @builtin-decl int builtin_suppresshistory(interpreter*) */
/* @builtin-bind { 'H', builtin_suppresshistory }, */
int builtin_suppresshistory(interpreter* interp) {
  interp->enable_history = 0;
  return 1;
}
