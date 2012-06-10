#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_for(interpreter*) */
/* @builtin-bind { 'f', builtin_for }, */
int builtin_for(interpreter* interp) {
  string sreg, sfrom, sto, body;
  byte reg;
  signed from, to, inc, i;
  int result = 1;

  if (!stack_pop_strings(interp, 4, &body, &sto, &sfrom, &sreg)) UNDERFLOW;

  if (sreg->len != 1) {
    print_error_s("Invalid register", sreg);
    goto error;
  }
  reg = string_data(sreg)[0];

  if (!string_to_int(sfrom, &from)) {
    print_error_s("Invalid integer", sfrom);
    goto error;
  }

  if (!string_to_int(sto, &to)) {
    print_error_s("Invalid integer", sto);
    goto error;
  }

  inc = to > from? 1 : -1;

  for (i = 0; (inc > 0? i < to : i > to); i += inc) {
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
  free(sreg);
  free(sfrom);
  free(sto);
  free(body);
  return result;

  error:
  /* Restore stack and return failure */
  stack_push(interp, sreg);
  stack_push(interp, sfrom);
  stack_push(interp, sto);
  stack_push(interp, body);
  return 0;
}

/* @builtin-decl int builtin_fors(interpreter*) */
/* @builtin-bind { 'F', builtin_fors }, */
int builtin_fors(interpreter* interp) {
  string sto, body;
  byte reg = 'i';
  signed to, inc, i;
  int result = 1;

  if (!stack_pop_strings(interp, 2, &body, &sto)) UNDERFLOW;

  if (!string_to_int(sto, &to)) {
    print_error_s("Invavid integer", sto);
    /* Restore the stack and return failure */
    stack_push(interp, sto);
    stack_push(interp, body);
    return 0;
  }

  inc = to >= 0? +1 : -1;

  for (i = 0; (inc > 0? i < to : i > to); i += inc) {
    free(interp->registers[reg]);
    interp->registers[reg] = int_to_string(i);
    result = exec_code(interp, body);
    if (!result) break;

    if (!string_to_int(interp->registers[reg], &i)) {
      print_error_s("Register altered to invalid integer",
                    interp->registers[reg]);
      result = 0;
      break;
    }
  }

  /* Done, clean up and return */
  touch_reg(interp, reg);
  free(sto);
  free(body);
  return result;
}
