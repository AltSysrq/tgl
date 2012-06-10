/* Defines globally-required structures used by the payload data commands. */

#ifndef PAYLOAD_H_
#define PAYLOAD_H_

#include "../tgl.h"
#include "../strings.h"

/* Per-interpreter data used to maintain the payload state. */
typedef struct payload_data {
  /* The current payload data, and the original top-level code. */
  string data, global_code;
  /* Properties */
  string data_start_delim, value_delim, output_kv_delim;
  int balance_paren, balance_brack, balance_brace, balance_angle;
  int trim_paren, trim_brack, trim_brace, trim_angle, trim_space;
} payload_data;

/* Initialises the given payload data to defaults. */
void payload_data_init(payload_data*);

/* Frees the contents of the given payload data. */
void payload_data_destroy(payload_data*);

/* Scans the given string for prefix data, storing it into the given
 * payload. Returns the part of the string which did not contain prefix data.
 */
string payload_extract_prefix(string, payload_data*);

#endif /* PAYLOAD_H_ */
