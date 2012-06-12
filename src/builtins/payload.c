#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"
#include "payload.h"

void payload_data_init(payload_data* p) {
  p->data = p->data_base = p->global_code = NULL;
  p->data_start_delim = convert_string(",$");
  p->value_delim = PAYLOAD_WS_DELIM;
  p->output_kv_delim = convert_string(",");
  p->balance_paren = p->balance_brack = p->balance_brace = 1;
  p->trim_paren = p->trim_brack = p->trim_brace = 1;
  p->balance_angle = p->trim_angle = 0;
  p->trim_space = 0;
}

void payload_data_destroy(payload_data* p) {
  if (p->data) free(p->data_base);
  if (p->data_start_delim > PAYLOAD_LINE_DELIM)
    free(p->data_start_delim);
  if (p->value_delim > PAYLOAD_LINE_DELIM)
    free(p->value_delim);
  if (p->output_kv_delim > PAYLOAD_LINE_DELIM)
    free(p->output_kv_delim);
}

string payload_extract_prefix(string code, payload_data* p) {
  /* TODO */
  return code;
}

/* If the character at *ix is an opening parenthesis character and that type is
 * set to be balanced according to the given payload, advance the index to the
 * matching closing character or to the end of string and return 1. Otherwise,
 * return 0.
 */
static int balance_parens(unsigned* ix, string str, payload_data* payload) {
  byte curr, closing;
  if (*ix >= str->len) return 0;

  curr = string_data(str)[*ix];
  switch (curr) {
  case '{':
    if (!payload->balance_brace) return 0;
    closing = '}';
    break;

  case '(':
    if (!payload->balance_paren) return 0;
    closing = ')';
    break;

  case '[':
    if (!payload->balance_brack) return 0;
    closing = ']';
    break;

  case '<':
    if (!payload->balance_angle) return 0;
    closing = '>';
    break;

  default:
    /* Not a paren-char */
    return 0;
  }

  /* If we get here, we must balance. */
  for (++*ix; *ix < str->len && string_data(str)[*ix] != closing; ++*ix)
    balance_parens(ix, str, payload);

  return 1;
}

/* Searches for the given delimiter within the given strings. On success, sets
 * left to one plus the index of the last character of the LHS, and right to
 * the index of the first character of the RHS. On failure, left and right are
 * unmodified. Returns whether the delimiter was found.
 */
static int find_delimiter(string delim, string haystack,
                          unsigned* left, unsigned* right,
                          payload_data* payload) {
  unsigned i, j;
  if (delim == PAYLOAD_WS_DELIM) {
    for (i = 0; i < haystack->len && !isspace(string_data(haystack)[i]); ++i)
      balance_parens(&i, haystack, payload);
    for (j = i; j < haystack->len &&  isspace(string_data(haystack)[j]); ++j);
    if (i == j)
      /* No delimiter found */
      return 0;

    /* OK */
    *left = i;
    *right = j;
    return 1;
  } else if (delim == PAYLOAD_LINE_DELIM) {
    for (i = 0; i < haystack->len && string_data(haystack)[i] != '\n' &&
           string_data(haystack)[i] != '\r'; ++i)
      balance_parens(&i, haystack, payload);
    if (i == haystack->len) return 0;

    /* OK */
    *left = i;
    *right = (i+1 < haystack->len && string_data(haystack)[i] == '\r' &&
              string_data(haystack)[i+1] == '\n'? i+2 : i+1);
    return 1;
  } else {
    /* Why can't there be memmem() like strstr()? */
    for (i = 0; i <= haystack->len - delim->len; ++i) {
      if (balance_parens(&i, haystack, payload)) continue;
      for (j = 0; j < delim->len &&
             string_data(haystack)[i+j] == string_data(delim)[j]; ++j);
      if (j == delim->len) {
        /* Matches */
        *left = i;
        *right = i+j;
        return 1;
      }
    }

    /* Not found */
    return 0;
  }
}

/* Like find_delimiter, but uses the whole string if no delimiter is found.
 * Either bounding argument may be null.
 */
static int find_opt_delim(string delim, string haystack,
                          unsigned* left, unsigned* right,
                          payload_data* payload) {
  unsigned sl, sr;
  if (!left) left = &sl;
  if (!right) right = &sr;

  *left = haystack->len;
  *right = haystack->len;
  find_delimiter(delim, haystack, left, right, payload);
  return 1;
}

/* Sets payload data to that extracted from code, using the current data-start
 * delimeter. Returns whether extraction succeeded.
 */
static int payload_from_code(interpreter* interp) {
  unsigned eoc, sop;
  string global_code = interp->payload.global_code;

  if (!global_code) {
    print_error("Embedded payload not available in this context.");
    return 0;
  }

  if (!find_delimiter(interp->payload.data_start_delim,
                      global_code, &eoc, &sop, &interp->payload)) {
    print_error("No embedded data found");
    return 0;
  }

  if (interp->payload.data)
    free(interp->payload.data_base);

  interp->payload.data = interp->payload.data_base =
    create_string(string_data(global_code)+sop,
                  string_data(global_code)+global_code->len);
  return 1;
}

/* Automatically extracts embedded payload data if none exists yet.
 *
 * Returns 1 on success, 0 if an error occurs.
 */
static int auto_payload(interpreter* interp) {
  if (interp->payload.data) return 1;
  return payload_from_code(interp);
}

/* Automatically call auto_payload and return failure if it fails. */
#define AUTO do { if (!auto_payload(interp)) return 0; } while (0)

/* Shortcut for accessing interp->payload.data */
#define DATA interp->payload.data

static int payload_start(interpreter* interp) {
  /* Jump past payload (to EOT). */
  interp->ip = interp->code->len;
  return 1;
}

static int payload_curr(interpreter* interp) {
  unsigned end;

  AUTO;

  if (!DATA->len) {
    print_error("No current item");
    return 0;
  }

  find_opt_delim(interp->payload.value_delim, DATA, &end, NULL,
                 &interp->payload);
  stack_push(interp, create_string(string_data(DATA),
                                   string_data(DATA)+end));
  return 1;
}

static int payload_next(interpreter* interp) {
  unsigned begin;

  AUTO;

  if (!DATA->len) {
    print_error("No next item");
    return 0;
  }

  find_opt_delim(interp->payload.value_delim, DATA, NULL, &begin,
                 &interp->payload);
  DATA = string_advance(DATA, begin);
  return 1;
}

extern int builtin_print(interpreter* interp);
static int payload_print(interpreter* interp) {
  return payload_curr(interp) && builtin_print(interp) &&
    payload_next(interp);
}

static int payload_next_kv(interpreter* interp) {
  return payload_next(interp) && payload_next(interp);
}

static int payload_print_kv(interpreter* interp) {
  if (!payload_print(interp)) return 0;

  if (interp->payload.output_kv_delim == PAYLOAD_LINE_DELIM)
    stack_push(interp, convert_string("\n"));
  else if (interp->payload.output_kv_delim == PAYLOAD_WS_DELIM)
    stack_push(interp, convert_string(" "));
  else
    stack_push(interp, dupe_string(interp->payload.output_kv_delim));

  if (!builtin_print(interp)) return 0;

  return payload_print(interp);
}

static int payload_read(interpreter* interp) {
  AUTO;

  stack_push(interp, dupe_string(DATA));
  return 1;
}

static int payload_write(interpreter* interp) {
  string s;
  if (DATA)
    free(interp->payload.data_base);

  if (!(s = stack_pop(interp))) UNDERFLOW;

  DATA = interp->payload.data_base = dupe_string(s);
  free(s);
  return 1;
}

static struct {
  byte name;
  native_command command;
} subcommands[] = {
  { '!', payload_from_code },
  { '$', payload_start },
  { 'c', payload_curr },
  { ',', payload_next },
  { ';', payload_next_kv },
  { '.', payload_print },
  { ':', payload_print_kv },
  { 'r', payload_read },
  { 'R', payload_write },
  {0,0},
};

/* @builtin-decl int builtin_payload(interpreter*) */
/* @builtin-bind { ',', builtin_payload }, */
int builtin_payload(interpreter* interp) {
  unsigned i;

  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Subcommand expected");
    return 0;
  }

  for (i = 0; subcommands[i].command; ++i)
    if (subcommands[i].name == curr(interp))
      return subcommands[i].command(interp);

  print_error("Unrecognised subcommand");
  return 0;
}
