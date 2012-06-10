#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_dupe(interpreter*) */
/* @builtin-bind { ':', builtin_dupe }, */
int builtin_dupe(interpreter* interp) {
  string s;

  if (!(s = stack_pop(interp))) UNDERFLOW;
  stack_push(interp, s);
  stack_push(interp, dupe_string(s));
  return 1;
}

/* @builtin-decl int builtin_drop(interpreter*) */
/* @builtin-bind { ';', builtin_drop }, */
int builtin_drop(interpreter* interp) {
  string s;
  if (!(s = stack_pop(interp))) UNDERFLOW;
  free(s);
  return 1;
}

/* @builtin-decl int builtin_swap(interpreter*) */
/* @builtin-bind { 'x', builtin_swap }, */
int builtin_swap(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &a, &b)) UNDERFLOW;
  stack_push(interp, a);
  stack_push(interp, b);
  return 1;
}
