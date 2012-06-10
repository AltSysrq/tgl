#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_code(interpreter*) */
/* @builtin-bind { '(', builtin_code }, */
int builtin_code(interpreter* interp) {
  unsigned depth = 1, begin = ++interp->ip;

  while (is_ip_valid(interp) && depth) {
    if (curr(interp) == '(') ++depth;
    if (curr(interp) == ')') --depth;
    if (depth) ++interp->ip;
  }

  /* If depth is not zero, we hit EOI. */
  if (depth) {
    print_error("Unbalanced parenthesis");
    return 0;
  }

  stack_push(interp, create_string(string_data(interp->code)+begin,
                                   string_data(interp->code)+interp->ip));
  return 1;
}

/* @builtin-decl int builtin_escape(interpreter*) */
/* @builtin-bind { '\\', builtin_escape }, */
/* In order to communicate with builtin_string, builtin_escape will return 2 to
 * indicate it did not push anything.
 */
int builtin_escape(interpreter* interp) {
  byte what, x0, x1;
  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Escaped character expected");
    return 0;
  }

  switch (curr(interp)) {
  case '(':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
  case '<':
  case '>': return 2;

  case 'a': what = '\a'; break;
  case 'b': what = '\b'; break;
  case 'e': what = '\033'; break;
  case 'f': what = '\f'; break;
  case 'n': what = '\n'; break;
  case 'r': what = '\r'; break;
  case 't': what = '\t'; break;
  case 'v': what = '\v'; break;
  case '"':
  case '\\':
  case '\'':
  case '$':
  case '%':
  case '`': what = curr(interp); break;
  case 'x': {
    /* Extract both digits if present. */
    ++interp->ip;
    if (is_ip_valid(interp)) {
      x0 = curr(interp);
      ++interp->ip;
      if (is_ip_valid(interp))
        x1 = curr(interp);
    }

    if (!is_ip_valid(interp)) {
      print_error("Two hexits expected after \\x");
      return 0;
    }

    /* Convert integer */
    if (x0 >= '0' && x0 <= '9')
      what = (x0 - '0') << 4;
    else if (x0 >= 'a' && x0 <= 'f')
      what = (x0 + 10 - 'a') << 4;
    else if (x0 >= 'A' && x0 <= 'F')
      what = (x0 + 10 - 'A') << 4;
    else {
      print_error("First \\x hexit is invalid");
      return 0;
    }
    if (x1 >= '0' && x1 <= '9')
      what |= (x1 - '0');
    else if (x1 >= 'a' && x1 <= 'f')
      what |= (x1 + 10 - 'a');
    else if (x1 >= 'A' && x1 <= 'F')
      what |= (x1 + 10 - 'A');
    else {
      print_error("Second \\x hexit is invalid");
      return 0;
    }
  } break;
  default:
    print_error("Invalid escape sequence");
    return 0;
  }

  /* Create and push the string. */
  stack_push(interp, create_string(&what, (&what)+1));
  return 1;
}

/* @builtin-decl int builtin_string(interpreter*) */
/* @builtin-bind { '"', builtin_string }, */
int builtin_string(interpreter* interp) {
  string accum, s;
  int result;
  unsigned begin;

  accum = empty_string();
  ++interp->ip;
  while (1) {
    /* Scan for the next important character. */
    for (begin = interp->ip; is_ip_valid(interp); ++interp->ip)
      if (curr(interp) == '"' ||
          curr(interp) == '$' ||
          curr(interp) == '`' ||
          curr(interp) == '\\' ||
          curr(interp) == '%')
        break;

    /* Found important character or EOI. */
    if (!is_ip_valid(interp)) {
      print_error("Encountered end-of-input in string literal");
      goto error;
    }

    /* Append everything in-between */
    accum = append_data(accum,
                        string_data(interp->code) + begin,
                        string_data(interp->code) + interp->ip);

    switch (curr(interp)) {
    case '"': goto done;
    case '$':
      ++interp->ip;
      if (!is_ip_valid(interp)) {
        print_error("Encountered end-of-input in string literal");
        goto error;
      }
      /* Append value of this register */
      accum = append_string(accum,
                            interp->registers[curr(interp)]);
      touch_reg(interp, curr(interp));
      break;

    case '%':
      s = stack_pop(interp);
      if (!s) {
        print_error("Stack underflow");
        goto error;
      }
      accum = append_string(accum, s);
      free(s);
      break;

    case '`':
      if (!interp->initial_whitespace) {
        print_error("Initial whitespace (`) not available in this context.");
        goto error;
      }
      accum = append_string(accum, interp->initial_whitespace);
      break;

    case '\\':
      result = builtin_escape(interp);
      if (!result) goto error;
      if (result == 2) break; /* Didn't push anything */
      s = stack_pop(interp);
      /* Popping will always succeed if escape returned success. */
      accum = append_string(accum, s);
      free(s);
      break;
    }

    /* All the above (which don't goto elsewhere) leave the IP on the end of
     * whatever special thing they did.
     */
    ++interp->ip;
  }

  done:
  stack_push(interp, accum);
  return 1;

  error:
  diagnostic(interp, NULL);
  free(accum);
  return 0;
}
