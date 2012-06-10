#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_and(interpreter*) */
/* @builtin-bind { '&', builtin_and }, */
int builtin_and(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  /* Need to use non-short-circuiting operator so that the strings get freed. */
  stack_push(interp, int_to_string(string_to_bool_free(a) &
                                   string_to_bool_free(b)));
  return 1;
}

/* @builtin-decl int builtin_or(interpreter*) */
/* @builtin-bind { '|', builtin_or }, */
int builtin_or(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  /* Use non-short-circuiting operator to free both strings. */
  stack_push(interp, int_to_string(string_to_bool_free(a) |
                                   string_to_bool_free(b)));
  return 1;
}

/* @builtin-decl int builtin_xor(interpreter*) */
/* @builtin-bind { '^', builtin_xor }, */
int builtin_xor(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  stack_push(interp, int_to_string(string_to_bool_free(a) ^
                                   string_to_bool_free(b)));
  return 1;
}

/* @builtin-decl int builtin_not(interpreter*) */
/* @builtin-bind { '~', builtin_not }, */
int builtin_not(interpreter* interp) {
  string a;
  if (!(a = stack_pop(interp))) UNDERFLOW;

  stack_push(interp, int_to_string(!string_to_bool_free(a)));
  return 1;
}
