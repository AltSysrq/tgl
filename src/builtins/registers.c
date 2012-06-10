#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_read(interpreter*) */
/* @builtin-bind { 'r', builtin_read }, */
int builtin_read(interpreter* interp) {
  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Register name expected");
    return 0;
  }

  stack_push(interp, dupe_string(interp->registers[curr(interp)]));
  touch_reg(interp, curr(interp));
  return 1;
}

/* @builtin-decl int builtin_write(interpreter*) */
/* @builtin-bind { 'R', builtin_write }, */
int builtin_write(interpreter* interp) {
  string val;

  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Register name expected");
    return 0;
  }

  if (!(val = stack_pop(interp))) UNDERFLOW;

  free(interp->registers[curr(interp)]);
  interp->registers[curr(interp)] = dupe_string(val);
  touch_reg(interp, curr(interp));
  return 1;
}

/* @builtin-decl int builtin_stash(interpreter*) */
/* @builtin-bind { 'p', builtin_stash }, */
int builtin_stash(interpreter* interp) {
  unsigned i;
  pstack_elt* elt;

  elt = tmalloc(sizeof(pstack_elt));
  for (i = 0; i < 256; ++i)
    elt->registers[i] = dupe_string(interp->registers[i]);
  elt->next = interp->pstack;
  interp->pstack = elt;

  return 1;
}

/* @builtin-decl int builtin_retrieve(interpreter*) */
/* @builtin-bind { 'P', builtin_retrieve }, */
int builtin_retrieve(interpreter* interp) {
  unsigned i;
  pstack_elt* top;

  if (!interp->pstack) {
    print_error("P-stack underflow");
    return 0;
  }

  top = interp->pstack;
  interp->pstack = top->next;

  /* Free current registers */
  for (i = 0; i < 256; ++i)
    free(interp->registers[i]);

  /* Restore old values */
  memcpy(interp->registers, top->registers, sizeof(interp->registers));
  free(top);
  return 1;
}

/* @builtin-decl int builtin_stashretrieve(interpreter*) */
/* @builtin-bind { 'z', builtin_stashretrieve }, */
int builtin_stashretrieve(interpreter* interp) {
  string s;
  if (!(s = stack_pop(interp))) UNDERFLOW;

  stack_push(interp,
             append_cstr(append_string(convert_string("p"), s), "P"));
  return 1;
}
