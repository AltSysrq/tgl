#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_number(interpreter*) */
/* @builtin-bind { '#', builtin_number }, */
/* @builtin-bind { '0', builtin_number }, */
/* @builtin-bind { '1', builtin_number }, */
/* @builtin-bind { '2', builtin_number }, */
/* @builtin-bind { '3', builtin_number }, */
/* @builtin-bind { '4', builtin_number }, */
/* @builtin-bind { '5', builtin_number }, */
/* @builtin-bind { '6', builtin_number }, */
/* @builtin-bind { '7', builtin_number }, */
/* @builtin-bind { '8', builtin_number }, */
/* @builtin-bind { '9', builtin_number }, */
int builtin_number(interpreter* interp) {
  unsigned begin;
  int has_digits = 0;

  /* Possible leading # which must be removed. */
  if (curr(interp) == '#') ++interp->ip;

  begin = interp->ip;

  /* Advance until no more integer-like characters can be read. */
  if (!is_ip_valid(interp)) goto end;
  /* Initial sign */
  if (curr(interp) == '+' || curr(interp) == '-')
    ++interp->ip;
  if (!is_ip_valid(interp)) goto end;

  /* If we hit a zero, we must check to see whether a base indicator follows. */
  if (curr(interp) == '0') {
    ++interp->ip;
    if (!is_ip_valid(interp)) {
      /* Isolated 0 */
      has_digits = 1;
      goto end;
    }

    if (curr(interp) == 'x' ||
        curr(interp) == 'X') {
      ++interp->ip;
      goto hex;
    }

    if (curr(interp) == 'b' ||
        curr(interp) == 'B') {
      ++interp->ip;
      goto bin;
    }

    if (curr(interp) == 'o' ||
        curr(interp) == 'O') {
      ++interp->ip;
      goto oct;
    }

    /* Valid leading zero, handle the rest of the number as decimal. */
    has_digits = 1;
  }

  /* Decimal */
  while (is_ip_valid(interp) &&
         (curr(interp) >= '0' && curr(interp) <= '9')) {
    ++interp->ip;
    has_digits = 1;
  }
  goto end;

  hex:
  while (is_ip_valid(interp) &&
         ((curr(interp) >= '0' && curr(interp) <= '9') ||
          (curr(interp) >= 'a' && curr(interp) <= 'f') ||
          (curr(interp) >= 'A' && curr(interp) <= 'F'))) {
    ++interp->ip;
    has_digits = 1;
  }
  goto end;

  oct:
  while (is_ip_valid(interp) &&
         (curr(interp) >= '0' && curr(interp) <= '7')) {
    ++interp->ip;
    has_digits = 1;
  }
  goto end;

  bin:
  while (is_ip_valid(interp) &&
         (curr(interp) >= '0' && curr(interp) <= '1')) {
    ++interp->ip;
    has_digits = 1;
  }
  goto end;

  end:
  if (!has_digits) {
    print_error("Integer literal expected");
    return 0;
  }

  /* OK */
  stack_push(interp, create_string(string_data(interp->code)+begin,
                                   string_data(interp->code)+interp->ip));
  /* Move back one space if we terminated due to landing on a non-digit
   * character */
  if (is_ip_valid(interp))
    --interp->ip;
  return 1;
}

/* @builtin-decl int builtin_add(interpreter*) */
/* @builtin-bind { '+', builtin_add }, */
int builtin_add(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;
  stack_push(interp, int_to_string(a+b));
  return 1;
}

/* @builtin-decl int builtin_sub(interpreter*) */
/* @builtin-bind { '-', builtin_sub }, */
int builtin_sub(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;
  stack_push(interp, int_to_string(a-b));
  return 1;
}

/* @builtin-decl int builtin_mul(interpreter*) */
/* @builtin-bind { '*', builtin_mul }, */
int builtin_mul(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;
  stack_push(interp, int_to_string(a*b));
  return 1;
}

/* @builtin-decl int builtin_div(interpreter*) */
/* @builtin-bind { '/', builtin_div }, */
int builtin_div(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;

  if (!b) {
    print_error("Division by zero");
    return 0;
  }

  stack_push(interp, int_to_string(a/b));
  return 1;
}

/* @builtin-decl int builtin_mod(interpreter*) */
/* @builtin-bind { '%', builtin_mod }, */
int builtin_mod(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;

  if (!b) {
    print_error("Division by zero");
    return 0;
  }

  stack_push(interp, int_to_string(a%b));
  return 1;
}

/* @builtin-decl int builtin_less(interpreter*) */
/* @builtin-bind { '<', builtin_less }, */
int builtin_less(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;

  stack_push(interp, int_to_string(a < b));
  return 1;
}

/* @builtin-decl int builtin_greater(interpreter*) */
/* @builtin-bind { '>', builtin_greater }, */
int builtin_greater(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;

  stack_push(interp, int_to_string(a > b));
  return 1;
}

/* @builtin-decl int builtin_rand(interpreter*) */
/* @builtin-bind { '?', builtin_rand }, */
int builtin_rand(interpreter* interp) {
  stack_push(interp, int_to_string(rand() & 0xFFFF));
  return 1;
}
