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
  p->trim_space = 1;
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

/* Trims extraneous characters from the given string, returning a possibly new
 * string with the characters removed. The old string is destroyed or returned.
 */
static string payload_trim(string orig_str, payload_data* payload) {
  string s = orig_str;
  unsigned i;
  byte start, end;

  /* Whitespace */
  if (payload->trim_space) {
    /* Trailing */
    while (s->len > 0 && isspace(string_data(s)[s->len-1]))
      --s->len;
    /* Leading */
    for (i = 0; i < s->len && isspace(string_data(s)[i]); ++i);
    s = string_advance(s, i);
  }

  /* Parens */
  if (s->len >= 2) {
    start = string_data(s)[0];
    end = string_data(s)[s->len - 1];
    if (payload->trim_brace && start == '{' && end == '}' ||
        payload->trim_brack && start == '[' && end == ']' ||
        payload->trim_paren && start == '(' && end == ')' ||
        payload->trim_angle && start == '<' && end == '>') {
      --s->len;
      s = string_advance(s, 1);
    }
  }

  if (s != orig_str) {
    /* Head not preserved, must return duplicate. */
    s = dupe_string(s);
    free(orig_str);
  }

  return s;
}

/* Searches for the given delimiter within the given strings. On success, sets
 * left to one plus the index of the last character of the LHS, and right to
 * the index of the first character of the RHS. On failure, left and right are
 * unmodified. Returns whether the delimiter was found.
 */
static int find_delimiter_from(string delim, string haystack,
                               unsigned starting_index,
                               unsigned* left, unsigned* right,
                               payload_data* payload) {
  unsigned i, j;
  if (delim == PAYLOAD_WS_DELIM) {
    for (i = starting_index;
         i < haystack->len && !isspace(string_data(haystack)[i]); ++i)
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
    for (i = starting_index;
         i < haystack->len && string_data(haystack)[i] != '\n' &&
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
    for (i = starting_index; i <= haystack->len - delim->len; ++i) {
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

/* Like find_delimiter_from, but assumes starting_index is zero. */
static int find_delimiter(string delim, string haystack,
                          unsigned* left, unsigned* right,
                          payload_data* payload) {
  return find_delimiter_from(delim, haystack, 0, left, right, payload);
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

static void set_payload(interpreter* interp, string data);

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

  set_payload(interp,
              create_string(string_data(global_code)+sop,
                            string_data(global_code)+global_code->len));
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
  stack_push(interp,
             payload_trim(create_string(string_data(DATA),
                                        string_data(DATA)+end),
                          &interp->payload));
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

static int payload_set_property(interpreter* interp) {
  byte pa, pb;
  string value;
  string* delim;

  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Missing property name following ,/");
    return 0;
  }
  pa = curr(interp);
  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Second property name character missing");
    return 0;
  }
  pb = curr(interp);

  if (!(value = stack_pop(interp))) UNDERFLOW;

  /* Macro to combine characters into 16-bit integers for the switch statement
   * below.
   */
#define S(a,b) (((unsigned)(a)<<8)|(b))
  switch (S(pa,pb)) {
  case S('p','s'):
    delim = &interp->payload.data_start_delim;
    goto set_delim;

  case S('v','d'):
    delim = &interp->payload.value_delim;
    goto set_delim;

  case S('o','k'):
    delim = &interp->payload.output_kv_delim;
    goto set_delim;

  case S('b','('):
    interp->payload.balance_paren = string_to_bool_free(value);
    break;

  case S('b','['):
    interp->payload.balance_brack = string_to_bool_free(value);
    break;

  case S('b','{'):
    interp->payload.balance_brace = string_to_bool_free(value);
    break;

  case S('b','<'):
    interp->payload.balance_angle = string_to_bool_free(value);
    break;

  case S('t','('):
    interp->payload.trim_paren = string_to_bool_free(value);
    break;

  case S('t','['):
    interp->payload.trim_brack = string_to_bool_free(value);
    break;

  case S('t','{'):
    interp->payload.trim_brace = string_to_bool_free(value);
    break;

  case S('t','<'):
    interp->payload.trim_angle = string_to_bool_free(value);
    break;

  case S('t','s'):
    interp->payload.trim_space = string_to_bool_free(value);
    break;

  default:
    print_error("Unrecognised property");
    stack_push(interp, value);
    return 0;
  }
#undef S

  return 1;

  set_delim:
  if (*delim > PAYLOAD_LINE_DELIM)
    free(*delim);

  if (value->len == 2 &&
      !memcmp("ws", string_data(value), 2)) {
    free(value);
    value = PAYLOAD_WS_DELIM;
  } else if (value->len == 2 &&
             !memcmp("lf", string_data(value), 2)) {
    free(value);
    value = PAYLOAD_LINE_DELIM;
  }

  *delim = value;
  return 1;
}

static int payload_get_property(interpreter* interp) {
  byte pa, pb;
  string delim;
  int boolean;

  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Missing property name following ,/");
    return 0;
  }
  pa = curr(interp);
  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Second property name character missing");
    return 0;
  }
  pb = curr(interp);

#define S(a,b) (((unsigned)(a)<<8)|(b))
  switch (S(pa, pb)) {
  case S('p','s'):
    delim = interp->payload.data_start_delim;
    goto get_delim;

  case S('v','d'):
    delim = interp->payload.value_delim;
    goto get_delim;

  case S('o','k'):
    delim = interp->payload.output_kv_delim;
    goto get_delim;

  case S('b','('):
    boolean = interp->payload.balance_paren;
    goto get_bool;

  case S('b','['):
    boolean = interp->payload.balance_brack;
    goto get_bool;

  case S('b','{'):
    boolean = interp->payload.balance_brace;
    goto get_bool;

  case S('b','<'):
    boolean = interp->payload.balance_angle;
    goto get_bool;

  case S('t','('):
    boolean = interp->payload.trim_paren;
    goto get_bool;

  case S('t','['):
    boolean = interp->payload.trim_brack;
    goto get_bool;

  case S('t','{'):
    boolean = interp->payload.trim_brace;
    goto get_bool;

  case S('t','<'):
    boolean = interp->payload.trim_angle;
    goto get_bool;

  case S('t','s'):
    boolean = interp->payload.trim_space;
    goto get_bool;

  default:
    print_error("Unrecognised property");
    return 0;
  }
#undef S

  get_delim:
  if (delim == PAYLOAD_WS_DELIM)
    delim = convert_string("ws");
  else if (delim == PAYLOAD_LINE_DELIM)
    delim = convert_string("lf");
  else
    delim = dupe_string(delim);
  stack_push(interp, delim);
  return 1;

  get_bool:
  stack_push(interp, int_to_string(boolean));
  return 1;
}

static int payload_length_bytes(interpreter* interp) {
  AUTO;

  stack_push(interp, int_to_string(DATA->len));
  return 1;
}

static int payload_num_indices(interpreter* interp) {
  unsigned off, ignore, cnt;
  AUTO;

  off = 0;
  cnt = 0;
  while (find_delimiter_from(interp->payload.value_delim,
                             DATA, off,
                             &ignore, &off, &interp->payload))
    ++cnt;

  /* If off is not at EOS, there is one more datum following. */
  if (off < DATA->len) ++cnt;

  /* Done */
  stack_push(interp, int_to_string(cnt));
  return 1;
}

static int payload_datum_at_index(interpreter* interp) {
  string six, scnt;
  signed ix, cnt;
  unsigned off, next, ignore;

  AUTO;

  if (!(six = stack_pop(interp))) UNDERFLOW;
  if (!string_to_int(six, &ix)) {
    print_error_s("Invalid integer", six);
    stack_push(interp, six);
    return 0;
  }

  /* If the index is negative, count how many items are left and add the count
   * to the index.
   */
  if (ix < 0) {
    payload_num_indices(interp);
    scnt = stack_pop(interp);
    string_to_int(scnt, &cnt);
    free(scnt);
    ix += cnt;
  }

  /* Bounds check.
   * The upper bound can't be determined till we fall off the end.
   */
  if (ix < 0) {
    print_error_s("Index out of range", six);
    stack_push(interp, six);
    return 0;
  }

  off = 0;
  while (off < DATA->len && ix > 0 &&
         find_delimiter_from(interp->payload.value_delim,
                             DATA, off,
                             &ignore, &off, &interp->payload))
    --ix;

  /* If ix > 0 or off == DATA->len, there is no item at this index.
   * Otherwise, the datum begins at off and ends at the next delimeter or EOS.
   */
  if (ix > 0 || off >= DATA->len) {
    print_error_s("Index out of range", six);
    stack_push(interp, six);
    return 0;
  }

  /* Set to EOS in case there is no next delimeter. */
  next = DATA->len;
  /* If there is a next delimiter, move next there; otherwise, leave it at
   * EOS. */
  find_delimiter_from(interp->payload.value_delim, DATA, off,
                      &next, &ignore, &interp->payload);

  /* Extract datum and return success. */
  stack_push(interp,
             payload_trim(create_string(string_data(DATA)+off,
                                        string_data(DATA)+next),
                          &interp->payload));
  free(six);
  return 1;
}

static int payload_datum_at_key(interpreter* interp) {
  string s, key;
  unsigned off, end, next;

  AUTO;

  if (!(s = stack_pop(interp))) UNDERFLOW;

  off = 0;
  /* We don't need to handle the case of the last item here (that is, an item
   * with no delim before EOS) since that would be an isolated key anyway.
   */
  while (off < DATA->len &&
         find_delimiter_from(interp->payload.value_delim,
                             DATA, off,
                             &end, &next, &interp->payload)) {
    key = payload_trim(create_string(string_data(DATA)+off,
                                     string_data(DATA)+end),
                       &interp->payload);
    off = next;
    if (!find_delimiter_from(interp->payload.value_delim,
                             DATA, off,
                             &end, &next, &interp->payload))
      end = next = DATA->len;

    if (string_equals(s, key)) {
      /* Found */
      free(s);
      free(key);
      stack_push(interp,
                 payload_trim(create_string(string_data(DATA)+off,
                                            string_data(DATA)+end),
                              &interp->payload));
      return 1;
    }

    off = next;
    free(key);
  }

  /* The loop only ends if the key is not found. */
  print_error_s("Key not found", s);
  stack_push(interp, s);
  return 0;
}

static int payload_space_delimited(interpreter* interp) {
  if (interp->payload.value_delim > PAYLOAD_LINE_DELIM)
    free(interp->payload.value_delim);

  interp->payload.value_delim = PAYLOAD_WS_DELIM;
  return 1;
}

static int payload_line_delimited(interpreter* interp) {
  if (interp->payload.value_delim > PAYLOAD_LINE_DELIM)
    free(interp->payload.value_delim);

  interp->payload.value_delim = PAYLOAD_LINE_DELIM;
  return 1;
}

static int payload_nul_delimited(interpreter* interp) {
  byte nul = 0;

  if (interp->payload.value_delim > PAYLOAD_LINE_DELIM)
    free(interp->payload.value_delim);

  interp->payload.value_delim = create_string(&nul, (&nul)+1);
  return 1;
}

/* Automatically replaces the interpreter's current payload, and performs any
 * implicit skipping needed.
 */
static void set_payload(interpreter* interp, string payload) {
  if (DATA)
    free(interp->payload.data_base);

  interp->payload.data = interp->payload.data_base = payload;
  /* Implicit skipping */
  if (DATA->len) {
    if ((isspace(string_data(DATA)[0]) &&
         interp->payload.value_delim == PAYLOAD_WS_DELIM) ||
        (('\n' == string_data(DATA)[0] || '\r' == string_data(DATA)[0]) &&
         interp->payload.value_delim == PAYLOAD_LINE_DELIM))
      payload_next(interp);
  }
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
  { '/', payload_set_property },
  { '?', payload_get_property },
  { 'h', payload_length_bytes },
  { 'i', payload_datum_at_index },
  { 'I', payload_num_indices },
  { 'k', payload_datum_at_key },
  { 's', payload_space_delimited },
  { 'l', payload_line_delimited },
  { '0', payload_nul_delimited },
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
