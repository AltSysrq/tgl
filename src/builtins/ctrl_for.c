#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_for(interpreter*) */
/* @builtin-bind { 'f', builtin_for }, */
int builtin_for(interpreter* interp) {
  string sto, body;
  byte reg = 'i';
  signed from = 0, to, inc, i;
  int result = 1, inc_is_explicit = 0;

  /* Examine secondary arguments */
  if (interp->u[0])
    if (!secondary_arg_as_int(interp->u[0], &from, 1))
      return 0;
  if (!secondary_arg_as_reg(interp->u[1], &reg)) return 0;
  if (interp->u[2]) {
    inc_is_explicit = 1;
    if (!secondary_arg_as_int(interp->u[2], &inc, 1))
      return 0;
    if (!inc) {
      print_error("Increnment must be non-zero");
      return 0;
    }
  }

  /* Get primary arguments */
  if (!stack_pop_strings(interp, 2, &body, &sto)) UNDERFLOW;

  if (!string_to_int(sto, &to)) {
    print_error_s("Invalid integer", sto);
    goto error;
  }

  /* Assign increment automatically, unless a specific one was requested. */
  if (!inc_is_explicit)
    inc = to > from? 1 : -1;

  for (i = from; (inc > 0? i < to : i > to); i += inc) {
    free(interp->registers[reg]);
    interp->registers[reg] = int_to_string(i);
    result = exec_code(interp, body);
    if (!result) break;
    /* Gracefully handle alterations to the register */
    if (!string_to_int(interp->registers[reg], &i)) {
      print_error_s("Register altered to invalid integer",
                    interp->registers[reg]);
      result = 0;
      break;
    }
  }

  /* Done, clean up and return result. */
  touch_reg(interp, reg);
  free(sto);
  free(body);
  reset_secondary_args(interp);
  return result;

  error:
  /* Restore stack and return failure */
  stack_push(interp, sto);
  stack_push(interp, body);
  return 0;
}

/* @builtin-decl int builtin_each(interpreter*) */
/* @builtin-bind { 'e', builtin_each }, */
int builtin_each(interpreter* interp) {
  string s, body;
  unsigned i;
  int status;
  byte reg = 'c';
  if (!secondary_arg_as_reg(interp->u[0], &reg))
    return 0;
  if (!stack_pop_strings(interp, 2, &body, &s)) UNDERFLOW;

  status = 1;
  for (i = 0; i < s->len && status; ++i) {
    free(interp->registers[reg]);
    interp->registers[reg] = create_string(string_data(s)+i,
                                           string_data(s)+i+1);
    touch_reg(interp, reg);
    status = exec_code(interp, body);
  }

  free(body);
  free(s);
  reset_secondary_args(interp);
  return status;
}
