#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
