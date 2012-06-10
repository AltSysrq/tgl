#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_while(interpreter*) */
/* @builtin-bind { 'w', builtin_while }, */
int builtin_while(interpreter* interp) {
  string condition, body, s;
  int result;

  if (!stack_pop_strings(interp, 2, &body, &condition)) UNDERFLOW;

  while (1) {
    result = exec_code(interp, condition);
    if (!result) break;
    if (!(s = stack_pop(interp))) {
      print_error("Stack underflow after evaluating condition");
      result = 0;
      break;
    }
    if (!string_to_bool_free(s)) break;

    result = exec_code(interp, body);
    if (!result) break;
  }

  free(condition);
  free(body);
  return result;
}

/* @builtin-decl int builtin_whiles(interpreter*) */
/* @builtin-bind { 'W', builtin_whiles }, */
int builtin_whiles(interpreter* interp) {
  string body, s;
  int result;

  if (!(body = stack_pop(interp))) UNDERFLOW;

  do {
    result = exec_code(interp, body);
    if (!result) break;
    if (!(s = stack_pop(interp))) {
      print_error("Stack underflow after evaluating body");
      result = 0;
      break;
    }
    if (!string_to_bool_free(s)) break;
  } while (1);

  free(body);
  return result;
}
