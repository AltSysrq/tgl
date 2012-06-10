#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <fnmatch.h>

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_context(interpreter*) */
/* @builtin-bind { '@', builtin_context }, */
int builtin_context(interpreter* interp) {
  byte subcommand;
  unsigned begin;
  char glob[256];

  int skip_match, negate_match;

  /* Get the subcommand */
  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Expected subcommand after @");
    return 0;
  }
  subcommand = curr(interp);

  switch (subcommand) {
  case '?':
    stack_push(interp, int_to_string(interp->context_active));
    return 1;

  case 's':
    stack_push(interp, convert_string(current_context));
    return 1;

  case 'e':
    stack_push(interp, convert_string(get_context_extension(current_context)));
    return 1;

  case '=':
    skip_match = 0, negate_match = 0;
    break;

  case '!':
    skip_match = 0, negate_match = 1;
    break;

  case '&':
    skip_match = !interp->context_active, negate_match = 0;
    break;

  case '|':
    skip_match = interp->context_active, negate_match = 0;
    break;

  case '^':
    skip_match = !interp->context_active, negate_match = 1;
    break;

  case 'v':
    skip_match = interp->context_active, negate_match = 1;
    break;

  default:
    print_error("Unknown @ subcommand");
    return 0;
  }

  /* Read the pattern in */
  ++interp->ip;
  begin = interp->ip;

  while (is_ip_valid(interp) && !isspace(curr(interp)))
    ++interp->ip;

  if (begin == interp->ip) {
    print_error("Context glob expected after @ subcommand");
    return 0;
  }

  if (interp->ip - begin >= sizeof(glob)) {
    print_error("Glob string too long");
    return 0;
  }

  /* Do nothing more if skip_match is true. */
  if (!skip_match) {
    memcpy(glob, string_data(interp->code) + begin,
           interp->ip - begin);
    glob[interp->ip - begin] = 0;
    /* Must check to see if it matches. */
    interp->context_active = !fnmatch(glob, current_context, 0) ^ negate_match;
  }

  /* Done */
  return 1;
}
