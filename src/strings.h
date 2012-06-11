/* Contains structures and functions for string manipulation. */

#ifndef STRINGS_H_
#define STRINGS_H_

/* Defines a length-prefixed string.
 * The string contents start at the byte after the struct itself.
 *
 * Strings may have NUL bytes embedded, and are not NUL-terminated.
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
string convert_string(char*);

/* Creates a string from a copy of the given memory region. */
string create_string(void*, void*);


/* Duplicates the given TGL string.
 * The string must be free()d by the caller.
 */
string dupe_string(string);

/* Creates and returns a new empty string. */
string empty_string();

/* Appends string a to string b, destroying a.
 * Returns a string representing the concatenated strings.
 */
string append_string(string, string);

/* Appends a cstring onto a, destroying a.
 * Returns a string representing the concatenated strings.
 */
string append_cstr(string, char*);

/* Appends a block of data onto a, destroying it.
 * Returns a string representing the concatenated strings.
 */
string append_data(string, void*, void*);

/* Returns whether the two given strings are equal. */
int string_equals(string, string);

/* Tries to interpret the given string as an integer.
 *
 * If successful, *dst is set to the result and 1 is returned. Otherwise, *dst
 * is unchanged and 0 is returned.
 */
int string_to_int(string, signed*);

/* Converts the given string to a boolean value. */
int string_to_bool(string);

/* Like string_to_bool(), but frees the string as well. */
int string_to_bool_free(string);

/* Converts the given integer to a string.
 *
 * The string must be freed by the caller.
 */
string int_to_string(signed);

/* Returns a pointer to the beginning of the current context's extension,
 * including the leading '.', or the current context itself if it contains no
 * extension.
 */
char* get_context_extension(char* context);

/* Advances the head of the given string by the given number of characters,
 * destroying the contents before the new head. Only sizeof(struct string)
 * bytes must be copied for this operation. The original pointer must be kept
 * to pass to free() later.
 *
 * On systems that require memory alignment, a more traditional mass-move is
 * performed if advancement would result in a non-aligned head. Because of
 * this, the caller must not relay on any particular relocation of the head.
 *
 * Whether memory alignment is required is determined the first time the
 * function is invoked.
 */
string string_advance(string, unsigned);

#endif /* STRINGS_H_ */
