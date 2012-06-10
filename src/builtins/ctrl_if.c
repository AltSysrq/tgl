#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_if(interpreter*) */
/* @builtin-bind { 'i', builtin_if }, */
int builtin_if(interpreter* interp) {
  string condition, then, otherwise;
  int result;

  if (!stack_pop_strings(interp, 3, &otherwise, &then, &condition)) UNDERFLOW;

  if (string_to_bool_free(condition))
    result = exec_code(interp, then);
  else
    result = exec_code(interp, otherwise);

  free(then);
  free(otherwise);
  return result;
}

/* @builtin-decl int builtin_ifs(interpreter*) */
/* @builtin-bind { 'I', builtin_ifs }, */
int builtin_ifs(interpreter* interp) {
  string condition, then;
  int result;

  if (!stack_pop_strings(interp, 2, &then, &condition)) UNDERFLOW;

  if (string_to_bool_free(condition))
    result = exec_code(interp, then);
  else
    result = 1;

  free(then);
  return result;
}
