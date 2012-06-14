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
  signed cnt;

  if (!secondary_arg_as_int(interp->u[0], &cnt, 0))
    return 0;

  if (!(s = stack_pop(interp))) UNDERFLOW;
  stack_push(interp, s);
  while (cnt-- > 0)
    stack_push(interp, dupe_string(s));

  reset_secondary_args(interp);

  return 1;
}

/* @builtin-decl int builtin_drop(interpreter*) */
/* @builtin-bind { ';', builtin_drop }, */
int builtin_drop(interpreter* interp) {
  string s;
  signed cnt, height;
  stack_elt* elt;

  if (!secondary_arg_as_int(interp->u[0], &cnt, 0))
    return 0;

  /* In order to be atomic, check the stack height first */
  height = 0;
  for (elt = interp->stack; elt && height < cnt; elt = elt->next)
    ++height;

  if (height < cnt) UNDERFLOW;

  while (cnt-- > 0) {
    if (!(s = stack_pop(interp))) UNDERFLOW;
    free(s);
  }

  reset_secondary_args(interp);

  return 1;
}

/* @builtin-decl int builtin_swap(interpreter*) */
/* @builtin-bind { 'x', builtin_swap }, */
int builtin_swap(interpreter* interp) {
  stack_elt* to_move, * above, * below;
  signed off;

  if (!secondary_arg_as_int(interp->u[0], &off, 1))
    return 0;

  if (!off) return 1; /* Nothing to do */

  if (off > 0) {
    /* Moving the top element down */
    to_move = interp->stack;
    for (below = to_move->next, above = NULL; below && off; --off) {
      above = below;
      below = below->next;
    }

    if (off) UNDERFLOW;

    /* OK */
    interp->stack = to_move->next;
    to_move->next = below;
    if (above)
      above->next = to_move;
  } else {
    /* Moving the -offth element up */
    off = -off;
    for (to_move = interp->stack, above = NULL; to_move && off; --off) {
      above = to_move;
      to_move = to_move->next;
    }

    if (off) UNDERFLOW;

    /* OK */
    below = to_move->next;
    above->next = below;
    to_move->next = interp->stack;
    interp->stack = to_move;
  }

  reset_secondary_args(interp);

  return 1;
}
