/* Main program for TGL (Text Generation Language). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define EXIT_OUT_OF_MEMORY 255

/* Versions of malloc and realloc that abort on memory exhaustion. */
static inline void* tmalloc(size_t size) {
  void* result = malloc(size);
  if (!result) {
    perror("malloc");
    exit(EXIT_OUT_OF_MEMORY);
  }
  return result;
}
static inline void* trealloc(void* ptr, size_t size) {
  void* result = realloc(ptr, size);
  if (!result) {
    perror("realloc");
    exit(EXIT_OUT_OF_MEMORY);
  }
  return result;
}

/* BEGIN: String handling (strings may have NUL bytes within, and are not
 * NUL-terminated).
 */

/* Defines a length-prefixed string.
 * The string contents start at the byte after the struct itself.
 */
typedef struct string { unsigned len; }* string;
typedef unsigned char byte;

/* Returns a pointer to the beginning of character data of the given string. */
static inline byte* string_data(string s) {
  byte* c = (byte*)s;
  return c + sizeof(struct string);
}

/* Converts a C string to a TGL string.
 * The string must be free()d by the caller.
 */
static string convert_string(char* str) {
  unsigned int len = strlen(str);
  string result = tmalloc(sizeof(struct string) + len);
  result->len = len;
  memcpy(string_data(result), str, len);
  return result;
}

/* Duplicates the given TGL string.
 * The string must be free()d by the caller.
 */
static string dupe_string(string str) {
  string result = tmalloc(sizeof(struct string) + str->len);
  memcpy(result, str, sizeof(struct string) + str->len);
  return result;
}

/* Creates and returns a new empty string. */
static string empty_string() {
  string result = tmalloc(sizeof(struct string));
  result->len = 0;
  return result;
}

/* Appends string a to string b, destroying a.
 * Returns a string representing the concatenated strings.
 */
static string append_string(string a, string b) {
  string result = trealloc(a, sizeof(struct string)+a->len+b->len);
  memcpy(string_data(result) + result->len, string_data(b), b->len);
  result->len += b->len;
  return result;
}

/* Appends a cstring onto a, destroying a.
 * Returns a string representing the concatenated strings.
 */
static string append_cstr(string a, const char* b) {
  unsigned blen = strlen(b);
  string result = trealloc(a, sizeof(struct string)+a->len+blen);
  memcpy(string_data(result) + result->len, b, blen);
  result->len += blen;
  return result;
}

/* Tries to interpret the given string as an integer.
 *
 * If successful, *dst is set to the result and 1 is returned. Otherwise, *dst
 * is unchanged and 0 is returned.
 */
static int string_to_int(string s, signed* dst) {
  int negative = 0;
  signed result = 0;
  unsigned i = 0, base = 10, digit;
  byte* dat = string_data(s);
  if (!s->len) return 0;

  //Check for leading sign
  if (dat[i] == '+') {
    ++i;
  } else if (dat[i] == '-') {
    ++i;
    negative = 1;
  }

  if (i >= s->len) return 0;

  //Possible leading base
  if (dat[i] == '0') {
    ++i;
    //Handle 0 by itself
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

  //Read the rest of the number
  for (; i < s->len; ++i) {
    if (dat[i] >= '0' && dat[i] <= '9')
      digit = dat[i] - '0';
    else if (dat[i] >= 'a' && dat[i] <= 'f')
      digit = dat[i] + 10 - 'a';
    else if (dat[i] >= 'A' && dat[i] <= 'F')
      digit = dat[i] + 10 - 'A';
    else
      return 0;

    //Ensure digit is valid in this base
    if (digit >= base)
      return 0;

    result *= base;
    result += digit;
  }

  //Everything was OK
  *dst = result;
  return 1;
}

/* Converts the given integer to a string.
 *
 * The string must be freed by the caller.
 */
static string int_to_string(signed i) {
  //Assuming that ever byte is three digits will always be sufficient.
  //Then add one for sign and one for term NUL
  static char buffer[2 + sizeof(signed)*3];
  sprintf(buffer, "%d", i);
  return convert_string(buffer);
}
/* END: String handling */

int main(int argc, char** argv) {
  printf("hello world\n");
  return 0;
}
