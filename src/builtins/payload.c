#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"
#include "payload.h"

void payload_data_init(payload_data* p) {
  p->data = p->global_code = NULL;
  p->data_start_delim = convert_string(",$");
  p->value_delim = PAYLOAD_WS_DELIM;
  p->output_kv_delim = convert_string(",");
  p->balance_paren = p->balance_brack = p->balance_brace = 1;
  p->trim_paren = p->trim_brack = p->trim_brace = 1;
  p->balance_angle = p->trim_angle = 0;
  p->trim_space = 0;
}

void payload_data_destroy(payload_data* p) {
  if (p->data) free(p->data);
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

/* Searches for the given delimiter within the given strings. On success, sets
 * left to one plus the index of the last character of the LHS, and right to
 * the index of the first character of the RHS. On failure, left and right are
 * unmodified. Returns whether the delimiter was found.
 */
static int find_delimiter(string delim, string haystack,
                          unsigned* left, unsigned* right) {
  unsigned i, j;
  if (delim == PAYLOAD_WS_DELIM) {
    for (i = 0; i < haystack->len && !isspace(string_data(haystack)[i]); ++i);
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
           string_data(haystack)[i] != '\r'; ++i);
    if (i == haystack->len) return 0;

    /* OK */
    *left = i;
    *right = (i+1 < haystack->len && string_data(haystack)[i] == '\r' &&
              string_data(haystack)[i+1] == '\n'? i+2 : i+1);
    return 1;
  } else {
    /* Why can't there be memmem() like strstr()? */
    for (i = 0; i <= haystack->len - delim->len; ++i) {
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

static struct {
  byte name;
  native_command command;
} subcommands[] = {
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
