#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

int builtin_number(interpreter*);

/* @builtin-decl int builtin_secondary_argument(interpreter*) */
/* @builtin-bind { 'u', builtin_secondary_argument }, */
int builtin_secondary_argument(interpreter* interp) {
  byte c;
  string s;
  unsigned i;
  stack_elt* stack;

  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Secondary argument specifier expected");
    return 0;
  }

  if (interp->ux >= NUM_SECONDARY_ARGS) {
    print_error("Too many secondary arguments");
    return 0;
  }

  c = curr(interp);
  switch (c) {
  case '%':
    if (!(s = stack_pop(interp))) UNDERFLOW;
    break;

  case ' ':
    s = NULL;
    break;

  case '.':
    i = 0;
    for (stack = interp->stack; stack; stack = stack->next)
      ++i;

    s = int_to_string(i);
    break;

  case '+':
  case '-':
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    if (!builtin_number(interp)) return 0;
    s = stack_pop(interp);
    break;

  default:
    s = create_string(&c, (&c)+1);
    break;
  }

  interp->u[interp->ux++] = s;
  return 1;
}
