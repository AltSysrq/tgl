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
static string covert_string(char* str) {
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
/* END: String handling */

int main(int argc, char** argv) {
  printf("hello world\n");
  return 0;
}
