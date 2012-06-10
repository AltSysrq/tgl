#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>

#include "tgl.h"
#include "strings.h"

string convert_string(char* str) {
  unsigned int len = strlen(str);
  string result = tmalloc(sizeof(struct string) + len);
  result->len = len;
  memcpy(string_data(result), str, len);
  return result;
}

string create_string(void* begin_, void* end_) {
  byte* begin = begin_, * end = end_;
  unsigned len = end-begin;
  string result = tmalloc(sizeof(struct string) + len);
  result->len = len;
  memcpy(string_data(result), begin, len);
  return result;
}

string dupe_string(string str) {
  string result = tmalloc(sizeof(struct string) + str->len);
  memcpy(result, str, sizeof(struct string) + str->len);
  return result;
}

string empty_string() {
  string result = tmalloc(sizeof(struct string));
  result->len = 0;
  return result;
}

string append_string(string a, string b) {
  string result = trealloc(a, sizeof(struct string)+a->len+b->len);
  memcpy(string_data(result) + result->len, string_data(b), b->len);
  result->len += b->len;
  return result;
}

string append_cstr(string a, char* b) {
  unsigned blen = strlen(b);
  string result = trealloc(a, sizeof(struct string)+a->len+blen);
  memcpy(string_data(result) + result->len, b, blen);
  result->len += blen;
  return result;
}

string append_data(string a, void* begin_, void* end_) {
  byte* begin = begin_, * end = end_;
  unsigned blen = end-begin;
  string result = trealloc(a, sizeof(struct string) + a->len + blen);
  memcpy(string_data(result) + result->len, begin, blen);
  result->len += blen;
  return result;
}

int string_equals(string a, string b) {
  if (a->len != b->len)
    return 0;

  return !memcmp(string_data(a), string_data(b), a->len);
}

int string_to_int(string s, signed* dst) {
  int negative = 0;
  signed result = 0;
  unsigned i = 0, base = 10, digit;
  byte* dat = string_data(s);
  if (!s->len) return 0;

  /* Check for leading sign */
  if (dat[i] == '+') {
    ++i;
  } else if (dat[i] == '-') {
    ++i;
    negative = 1;
  }

  if (i >= s->len) return 0;

  /* Possible leading base */
  if (dat[i] == '0') {
    ++i;
    /* Handle 0 by itself */
    if (i == s->len) {
      *dst = 0;
      return 1;
    }

    if (dat[i] == 'x' || dat[i] == 'X') {
      base = 16;
      ++i;
      if (i >= s->len) return 0;
    } else if (dat[i] == 'b' || dat[i] == 'B') {
      base = 2;
      ++i;
      if (i >= s->len) return 0;
    } else if (dat[i] == 'o' || dat[i] == 'O') {
      base = 8;
      ++i;
      if (i >= s->len) return 0;
    }
  }

  /* Read the rest of the number */
  for (; i < s->len; ++i) {
    if (dat[i] >= '0' && dat[i] <= '9')
      digit = dat[i] - '0';
    else if (dat[i] >= 'a' && dat[i] <= 'f')
      digit = dat[i] + 10 - 'a';
    else if (dat[i] >= 'A' && dat[i] <= 'F')
      digit = dat[i] + 10 - 'A';
    else
      return 0;

    /* Ensure digit is valid in this base */
    if (digit >= base)
      return 0;

    result *= base;
    result += digit;
  }

  /* Everything was OK */
  if (negative) result = -result;
  *dst = result;
  return 1;
}

int string_to_bool(string s) {
  signed a;
  if (string_to_int(s, &a))
    return a != 0;
  else
    return s->len > 0;
}

int string_to_bool_free(string s) {
  int result = string_to_bool(s);
  free(s);
  return result;
}

string int_to_string(signed i) {
  /* Assuming that ever byte is three digits will always be sufficient.
   * Then add one for sign and one for term NUL.
   */
  static char buffer[2 + sizeof(signed)*3];
  sprintf(buffer, "%d", i);
  return convert_string(buffer);
}

char* get_context_extension(char* cxt) {
  unsigned len;
  char* ret;

  len = strlen(cxt);
  for (ret = cxt+len; ret != cxt && *ret != '.'; --ret);
  return ret;
}