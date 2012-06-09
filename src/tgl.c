/* Main program for TGL (Text Generation Language). */

/* TODO: Have configure define this. */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#ifdef _GNU_SOURCE
#include <getopt.h>
#endif /* _GNU_SOURCE */

#define EXIT_PROGRAM_ERROR 1
#define EXIT_IO_ERROR 254
#define EXIT_OUT_OF_MEMORY 255
#define EXIT_HELP 253

/* Globals */
/* The name of the user library file. */
static char* user_library_file;

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

/* Creates a string from a copy of the given memory region. */
static string create_string(void* begin_, void* end_) {
  byte* begin = begin_, * end = end_;
  unsigned len = end-begin;
  string result = tmalloc(sizeof(struct string) + len);
  result->len = len;
  memcpy(string_data(result), begin, len);
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
static string append_cstr(string a, char* b) {
  unsigned blen = strlen(b);
  string result = trealloc(a, sizeof(struct string)+a->len+blen);
  memcpy(string_data(result) + result->len, b, blen);
  result->len += blen;
  return result;
}

/* Appends a block of data onto a, destroying it.
 * Returns a string representing the concatenated strings.
 */
static string append_data(string a, void* begin_, void* end_) {
  byte* begin = begin_, * end = end_;
  unsigned blen = end-begin;
  string result = trealloc(a, sizeof(struct string) + a->len + blen);
  memcpy(string_data(result) + result->len, begin, blen);
  result->len += blen;
  return result;
}

/* Returns whether the two given strings are equal. */
static int string_equals(string a, string b) {
  if (a->len != b->len)
    return 0;

  return !memcmp(string_data(a), string_data(b), a->len);
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

/* Like string_to_int(), but also frees the input string if successful. */
static int string_to_int_free(string s, signed* dst) {
  int success = string_to_int(s, dst);
  if (success)
    free(s);

  return success;
}

/* Converts the given string to a boolean value. */
static int string_to_bool(string s) {
  signed a;
  if (string_to_int(s, &a))
    return a != 0;
  else
    return s->len > 0;
}

/* Like string_to_bool(), but frees the string as well. */
static int string_to_bool_free(string s) {
  int result = string_to_bool(s);
  free(s);
  return result;
}

/* Converts the given integer to a string.
 *
 * The string must be freed by the caller.
 */
static string int_to_string(signed i) {
  /* Assuming that ever byte is three digits will always be sufficient.
   * Then add one for sign and one for term NUL.
   */
  static char buffer[2 + sizeof(signed)*3];
  sprintf(buffer, "%d", i);
  return convert_string(buffer);
}
/* END: String handling */

/* BEGIN: Interpreter operations */
static void print_error(char*);
static void print_error_s(char*, string);

/* Represents a single stack element. */
typedef struct stack_elt {
  /* The datum held in this element. */
  string value;
  /* The next item in the stack, or NULL if this is the bottom. */
  struct stack_elt* next;
} stack_elt;

/* Represents a P-stack (register backup) element. */
typedef struct pstack_elt {
  /* Copies of all register values. */
  string registers[256];
  /* The next item in the stack, or NULL if this is the bottom. */
  struct pstack_elt* next;
} pstack_elt;

/* Defined later. */
struct interpreter;

/* Defines the function pointer type for native commands.
 *
 * A command gets solely the interpreter running it as an argument. The command
 * may modify the interpreter arbitrarily, though changing the instruction
 * string will have unexpected behaviour (at least to the user). The
 * instruction pointer is not incremented until the command returns, and thus
 * points at the instruction that invoked the command.
 *
 * The command returns 1 to indicate success, and 0 to indicate failure. If
 * failure is a direct result of this command, the command should print a
 * diagnostic to stderr before returning.
 */
typedef int (*native_command)(struct interpreter*);

/* Defines any type of command, either native or user-defined. */
typedef struct command {
  /* Whether the command is native. 1=native, 0=user. */
  int is_native;
  /* Pointer to either a native_command to call or a string to interpret.
   * A non-existent command is indicated by the pointers below being NULL.
   */
  union {
    native_command native;
    string user;
  } cmd;
} command;

/* Describes a long command binding.
 *
 * Any command whose name is longer than one command is considered a "long
 * command". Long commands are stored in a linked list.
 *
 * While this is hardly optimal, long commands are the exception rather than
 * the rule, and performance isn't critical anyway.
 */
typedef struct long_command {
  /* The name of this command. */
  string name;
  /* The command bound to this name. */
  command cmd;
  /* The next long_command in the list, or NULL if it is the last one. */
  struct long_command* next;
} long_command;

/* Defines all the data needed to interpret TGL code. */
typedef struct interpreter {
  /* All short commands, indexed by character. */
  command commands[256];
  /* All registers. Initially initialised to empty strings. */
  string registers[256];
  /* The last time of access for each register. */
  time_t reg_access[256];
  /* The stack, initially NULL. */
  stack_elt* stack;
  /* The P-stack, initially NULL. */
  pstack_elt* pstack;
  /* The list of long commands, initially NULL. */
  long_command* long_commands;
  /* The string currently being executed (NOT owned by the interpreter) */
  string code;
  /* The current instruction pointer within the code. */
  unsigned ip;
  /* The name of the current context. */
  string context;
  /* Whether the current context is active. */
  int context_active;
  /* The whitespace characters that were skipped before the first command was
   * executed. This is NULL before the first command is executed.
   */
  string initial_whitespace;
  /* Whether to enable saving history. */
  int enable_history;
  /* The current implicit offset for the h command. */
  unsigned history_offset;
} interpreter;

/* Pushes the given string onto the stack of the given interpreter.
 *
 * After this call, the caller must not free the string, as its presence on the
 * stack means it will be freed by someone else. Because of this, the same
 * string object cannot be pushed onto the stack more than once.
 */
static void stack_push(interpreter* interp, string val) {
  stack_elt* s = tmalloc(sizeof(stack_elt));
  s->value = val;
  s->next = interp->stack;
  interp->stack = s;
}

/* Macro for the common case of printing a stack underflow error and returning
 * failure.
 */
#define UNDERFLOW do { print_error("Stack underflow"); return 0; } while(0)

/* Pops an item of the stack of the interpreter and returns it.
 *
 * Returns NULL if the stack is empty.
 *
 * It is up to the caller to ensure the string is freed.
 */
static string stack_pop(interpreter* interp) {
  stack_elt* next;
  string result = NULL;
  if (interp->stack) {
    next = interp->stack->next;
    result = interp->stack->value;
    free(interp->stack);
    interp->stack = next;
  }
  return result;
}

/* Pops n strings from the stack as an atomic operation.
 *
 * Following the argument n should be n string*s, which will be set to the
 * popped strings if successful, and unchanged otherwise. The strings are
 * popped from left to right, which means they will be in the opposite order as
 * they were pushed (that is, the element pushed visually on the leftmost will
 * be extracted into the rightmost string).
 *
 * Returns 1 if successful, 0 otherwise.
 */
static int stack_pop_strings(interpreter* interp, unsigned n, ...) {
  va_list args;
  stack_elt* curr;
  unsigned cnt;
  /* Ensure the stack is big enough */
  for (cnt = 0, curr = interp->stack; cnt<n && curr; ++cnt, curr = curr->next);

  if (cnt < n)
    return 0; /* Not big enough */

  /* Pop the strings. */
  va_start(args, n);
  while (n-- > 0)
    *va_arg(args, string*) = stack_pop(interp);
  va_end(args);
  return 1;
}

/* Like stack_pop_strings(), but writes into an array instead of variadic
 * arguments.
 */
static int stack_pop_array(interpreter* interp, unsigned n, string dst[]) {
  stack_elt* curr;
  unsigned cnt;

  /* Ensure the stack is big enough */
  for (cnt = 0, curr = interp->stack; cnt<n && curr; ++cnt, curr = curr->next);

  if (cnt < n)
    return 0; /* Not big enough */

  /* Pop them */
  while (n-- > 0)
    *dst++ = stack_pop(interp);

  return 1;
}

/* Like stack_pop_strings(), but converts arguments to integers (signed*s).
 *
 * This function is not entirely atomic: If popping is successful, but
 * conversion fails, some of the integers may have been altered. In any case,
 * either the function is successful or the stack is unchanged.
 *
 * This function will produce it's own error messages on failure, since it
 * cannot indicate specifics to the caller.
 */
static int stack_pop_ints(interpreter* interp, unsigned n, ...) {
  string values[n];
  va_list args;
  unsigned i;

  if (!stack_pop_array(interp, n, values))
    UNDERFLOW;

  /* Try to convert them */
  va_start(args, n);
  for (i = 0; i < n; ++i) {
    if (!string_to_int(values[i], va_arg(args, signed*))) {
      /* Conversion failed, push the values back onto the stack and return
       * failure.
       */
      print_error_s("Bad integer", values[i]);
      for (i = n; i > 0; --i)
        stack_push(interp, values[i-1]);

      return 0;
    }
  }
  va_end(args);

  /* Free the strings and return success. */
  for (i = 0; i < n; ++i)
    free(values[i]);

  return 1;
}

/* Returns the character at the current code point in the given interpreter.
 *
 * Assumes that the IP is valid.
 */
static inline byte curr(interpreter* interp) {
  return string_data(interp->code)[interp->ip];
}

/* Returns whether the IP of the given interpreter is valid. */
static inline int is_ip_valid(interpreter* interp) {
  return interp->ip < interp->code->len;
}

/* Touches the register of the given name in the given VM. */
static void touch_reg(interpreter* interp, byte reg) {
  interp->reg_access[reg] = time(0);
}

/* Prints an error message to the user. */
static void print_error(char* message) {
  fprintf(stderr, "tgl: error: %s\n", message);
}

/* Like print_error, but includes the given string. */
static void print_error_s(char* message, string append) {
  fprintf(stderr, "tgl: error: %s: ", message);
  fwrite(string_data(append), 1, append->len, stderr);
  fprintf(stderr, "\n");
}

/* Prints a diagnostic to the user, including the given error message.
 * If the message is omitted, only context is shown.
 */
static void diagnostic(interpreter* interp, char* message) {
  char context[33], while_exec[] = "While executing: ";
  unsigned offset, len, i;
  /* Give the user up to 32 characters of context. */
  if (interp->ip >= 16)
    offset = interp->ip - 16;
  else
    offset = 0;
  if (offset + 32 < interp->code->len)
    len = 32;
  else
    len = interp->code->len - offset;
  memcpy(context, string_data(interp->code)+offset, len);
  context[len] = 0;
  /* Make sure all WS characters are spaces. */
  for (i = 0; i < len; ++i)
    if (isspace(context[i]))
      context[i] = ' ';

  if (message)
    print_error(message);
  fprintf(stderr, "%s%s\n%*s\n",
          while_exec, context,
          (unsigned)(interp->ip - offset + sizeof(while_exec)), "^");
}

static int exec_code(interpreter* interp, string code);

/* Executes a single instruction within the given interpreter.
 *
 * Returns 1 if successful or if the IP is past the end of the code, or 0 for
 * any kind of failure.
 *
 * Any whitespace encountered will be skipped. If there is no more code to
 * execute, the IP is not changed further. Otherwise, an instruction is
 * executed and, if successful, the IP is incremented.
 *
 * If any error occurs, the IP will be what it was before this function was
 * called, regardless of what any command may have done, except that whitespace
 * is still skipped (so the ip points to the instruction that was/would have
 * been executed).
 */
static int exec_one_command(interpreter* interp) {
  byte command;
  int success;
  unsigned old_ip;
  /* Skip whitespace */
  while (interp->ip < interp->code->len &&
         isspace(string_data(interp->code)[interp->ip]))
    ++interp->ip;

  /* Return success if nothing is left. */
  if (interp->ip >= interp->code->len)
    return 1;

  old_ip = interp->ip;

  /* Ensure the command exists */
  command = string_data(interp->code)[interp->ip];
  if (!interp->commands[command].cmd.native) {
    diagnostic(interp, "No such command");
    return 0;
  }

  /* Execute the command. */
  if (interp->commands[command].is_native)
    success = interp->commands[command].cmd.native(interp);
  else
    success = exec_code(interp, interp->commands[command].cmd.user);

  /* Move to next command if successful, then return. */
  if (success)
    ++interp->ip;
  else {
    interp->ip = old_ip;
    /* Show a simple diagnostic; this will create a sort of stack trace in cases
     * of nested code.
     */
    diagnostic(interp, NULL);
  }
  return success;
}

/* Executes the given code in the given interpreter.
 *
 * This will temprorarily alter the code and ip fields of the interpreter, but
 * they will be restored before the function returns.
 */
static int exec_code(interpreter* interp, string code) {
  string old_code;
  unsigned old_ip;
  int success;

  /* Back current values up */
  old_code = interp->code;
  old_ip = interp->ip;

  /* Set new values for execution */
  interp->code = code;
  interp->ip = 0;
  success = 1;
  /* Execute to completion or failure. */
  while (interp->ip < interp->code->len && success)
    success = exec_one_command(interp);

  /* Restore old values */
  interp->code = old_code;
  interp->ip = old_ip;

  return success;
}

extern struct builtins_t { char name; native_command cmd; } * builtins;
/* Initialises the given interpreter. */
static void interp_init(interpreter* interp) {
  unsigned i;

  memset(interp, 0, sizeof(interpreter));
  interp->context_active = 1;

  for (i=0; i < 256; ++i)
    interp->registers[i] = empty_string();
  for (i=0; builtins[i].name; ++i) {
    interp->commands[(unsigned)builtins[i].name].is_native = 1;
    interp->commands[(unsigned)builtins[i].name].cmd.native = builtins[i].cmd;
  }
}
/* END: Interpreter operations */

/* BEGIN: Built-in commands */
static int builtin_long_command(interpreter* interp) {
  unsigned begin, end;
  string commandName;
  long_command* curr;
  int result;

  /* Extract the long name */
  begin = ++interp->ip;
  while (interp->ip < interp->code->len &&
         !isspace(string_data(interp->code)[interp->ip]))
    ++interp->ip;

  end = interp->ip;
  if (begin == end || begin+1 == end) {
    print_error("Long command name expected");
    return 0;
  }

  commandName = create_string(string_data(interp->code)+begin,
                              string_data(interp->code)+end);

  /* Find the first long command name which matches. */
  curr = interp->long_commands;
  while (curr && !string_equals(curr->name, commandName))
    curr = curr->next;

  /* Don't need the command name anymore. */
  free(commandName);

  if (!curr) {
    print_error("Long command not found");
    return 0;
  }

  /* Execute, clean up, and return */
  if (curr->cmd.is_native)
    result = curr->cmd.cmd.native(interp);
  else
    result = exec_code(interp, curr->cmd.cmd.user);

  return result;
}

static int builtin_print(interpreter* interp) {
  string str;

  if (!(str = stack_pop(interp))) UNDERFLOW;

  fwrite(string_data(str), str->len, 1, stdout);
  if (ferror(stdout)) {
    free(str);
    print_error(strerror(errno));
    return 0;
  }

  free(str);
  return 1;
}

static int builtin_char(interpreter* interp) {
  string str;
  char cstr[2];

  ++interp->ip;

  if (interp->ip >= interp->code->len) {
    print_error("Expected character");
    return 0;
  }

  cstr[0] = string_data(interp->code)[interp->ip];
  cstr[1] = 0;
  str = convert_string(cstr);
  stack_push(interp, str);
  return 1;
}

static int builtin_dupe(interpreter* interp) {
  string s;

  if (!(s = stack_pop(interp))) UNDERFLOW;
  stack_push(interp, s);
  stack_push(interp, dupe_string(s));
  return 1;
}

static int builtin_drop(interpreter* interp) {
  string s;
  if (!(s = stack_pop(interp))) UNDERFLOW;
  free(s);
  return 1;
}

static int builtin_swap(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &a, &b)) UNDERFLOW;
  stack_push(interp, a);
  stack_push(interp, b);
  return 1;
}

static int builtin_concat(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  a = append_string(a, b);
  free(b);

  stack_push(interp, a);
  return 1;
}

static int builtin_length(interpreter* interp) {
  string s;
  if (!(s = stack_pop(interp))) UNDERFLOW;

  stack_push(interp, int_to_string(s->len));
  free(s);

  return 1;
}

static int builtin_charat(interpreter* interp) {
  string str, six;
  signed ix;
  if (!stack_pop_strings(interp, 2, &six, &str)) UNDERFLOW;

  if (!string_to_int(six, &ix)) {
    print_error_s("Bad integer", six);
    stack_push(interp, six);
    stack_push(interp, str);
    return 0;
  }

  if (ix < 0) ix += str->len;
  if (ix < 0 || ix >= str->len) {
    print_error_s("Integer out of range", six);
    stack_push(interp, six);
    stack_push(interp, str);
    return 0;
  }

  stack_push(interp, create_string(&string_data(str)[ix],
                                   &string_data(str)[ix+1]));

  free(str);
  free(six);
  return 1;
}

static int builtin_substr(interpreter* interp) {
  string str, sfrom, sto, result;
  signed from, to;

  if (!stack_pop_strings(interp, 3, &sto, &sfrom, &str)) UNDERFLOW;
  if (!string_to_int(sfrom, &from)) {
    print_error_s("Bad integer", sfrom);
    goto error;
  }
  if (!string_to_int(sto, &to)) {
    print_error_s("Bad integer", sto);
    goto error;
  }

  /* Cap the indices instead of generating errors. */
  if (from < 0) from += str->len;
  if (from < 0) from = 0;
  if (from > str->len) from = str->len;
  if (to < 0) to += str->len + 1;
  if (to < from) to = from;
  if (to > str->len) to = str->len;

  /* OK */
  result = create_string(string_data(str)+from,
                         string_data(str)+to);
  free(str);
  free(sfrom);
  free(sto);
  stack_push(interp, result);
  return 1;

  error:
  stack_push(interp, str);
  stack_push(interp, sfrom);
  stack_push(interp, sto);
  return 0;
}

static int builtin_suffix(interpreter* interp) {
  string str, sfrom, result;
  signed from;

  if (!stack_pop_strings(interp, 2, &sfrom, &str)) UNDERFLOW;
  if (!string_to_int(sfrom, &from)) {
    print_error_s("Bad integer", sfrom);
    stack_push(interp, str);
    stack_push(interp, sfrom);
    return 0;
  }

  /* Cap the indices instead of generating errors. */
  if (from < 0) from += str->len;
  if (from < 0) from = 0;
  if (from > str->len) from = str->len;

  /* OK */
  result = create_string(string_data(str)+from,
                         string_data(str)+str->len);
  free(str);
  free(sfrom);
  stack_push(interp, result);
  return 1;
}

static int builtin_map(interpreter* interp) {
  string str, *mapping, sn, result;
  signed n;
  unsigned i, j, k, bufferSize, bufferIx;
  byte* buffer;

  if (!(sn = stack_pop(interp))) UNDERFLOW;
  if (!string_to_int(sn, &n)) {
    print_error_s("Bad integer", sn);
    stack_push(interp, sn);
    return 0;
  }

  if (n < 0 || n >= 65536) {
    print_error_s("Invalid mapping size", sn);
    stack_push(interp, sn);
    return 0;
  }

  mapping = tmalloc(sizeof(string) * n * 2);
  if (!stack_pop_array(interp, n*2, mapping)) {
    free(mapping);
    stack_push(interp, sn);
    UNDERFLOW;
  }

  if (!(str = stack_pop(interp))) {
    for (i = n*2; i > 0; --i)
      stack_push(interp, mapping[i-1]);
    free(mapping);
    stack_push(interp, sn);
    UNDERFLOW;
  }

  /* Now that we have all the parms, determine the buffer size for building the
   * result.
   *
   * The buffer size must be at least twice the size of the longest
   * substitution, but use 1024 if nothing requires that.
   */
  bufferSize = 1024;
  for (i = n*2; i > 0; i -= 2)
    if (2 * mapping[i-2]->len > bufferSize)
      bufferSize = 2 * mapping[i-2]->len;
  buffer = malloc(bufferSize);

  /* For each character in the input string, see if it matches any mapping
   * source. If it does, see if the whole source matches. If it does, copy the
   * mapping destination into the buffer; otherwise, keep looking. If no
   * mapping matches, copy that character into the buffer. In either case,
   * advance the index past the character(s) that were copied. When the buffer
   * usage exceeds half the buffer size, append the buffer to the result and
   * clear the buffer.
   */
  bufferIx = 0;
  i = 0;
  result = empty_string();
  while (i < str->len) {
    for (j = n*2; j > 0; --j) {
      /* Silently ignore mappings with an empty string source */
      if (!mapping[j-1]->len) continue;
      if (string_data(str)[i] == string_data(mapping[j-1])[0]) {
        /* Does the rest match? */
        if (str->len - i < mapping[j-1]->len)
          /* Source is longer than remaining input */
          continue;

        for (k = 1; k < mapping[j-1]->len; ++k)
          if (string_data(str)[i+k]  != string_data(mapping[j-1])[k])
            goto source_not_match;

        /* If we get here, the two match. Substitute with the destination and
         * advance the length of the source.
         */
        memcpy(buffer+bufferIx, string_data(mapping[j-2]), mapping[j-2]->len);
        bufferIx += mapping[j-2]->len;
        i += mapping[j-1]->len;
        goto appended_to_buffer;
      }

      source_not_match:;
    }

    /* If we get here, no source matched, so just copy the input character. */
    buffer[bufferIx++] = string_data(str)[i++];

    appended_to_buffer:
    /* If the buffer is half-full or more, move to result. */
    if (bufferIx >= bufferSize/2) {
      result = append_data(result, buffer, buffer+bufferIx);
      bufferIx = 0;
    }
  }

  /* Copy any remaining buffer to the result. */
  result = append_data(result, buffer, buffer+bufferIx);

  /* Clean up and return result. */
  for (i = 0; i < n*2; ++i)
    free(mapping[i]);
  free(sn);
  free(str);
  free(buffer);
  free(mapping);

  stack_push(interp, result);
  return 1;
}

static int builtin_number(interpreter* interp) {
  unsigned begin;
  int has_digits = 0;

  /* Possible leading # which must be removed. */
  if (curr(interp) == '#') ++interp->ip;

  begin = interp->ip;

  /* Advance until no more integer-like characters can be read. */
  if (!is_ip_valid(interp)) goto end;
  /* Initial sign */
  if (curr(interp) == '+' || curr(interp) == '-')
    ++interp->ip;
  if (!is_ip_valid(interp)) goto end;

  /* If we hit a zero, we must check to see whether a base indicator follows. */
  if (curr(interp) == '0') {
    ++interp->ip;
    if (!is_ip_valid(interp)) {
      /* Isolated 0 */
      has_digits = 1;
      goto end;
    }

    if (curr(interp) == 'x' ||
        curr(interp) == 'X') {
      ++interp->ip;
      goto hex;
    }

    if (curr(interp) == 'b' ||
        curr(interp) == 'B') {
      ++interp->ip;
      goto bin;
    }

    if (curr(interp) == 'o' ||
        curr(interp) == 'O') {
      ++interp->ip;
      goto oct;
    }

    /* Valid leading zero, handle the rest of the number as decimal. */
    has_digits = 1;
  }

  /* Decimal */
  while (is_ip_valid(interp) &&
         (curr(interp) >= '0' && curr(interp) <= '9')) {
    ++interp->ip;
    has_digits = 1;
  }
  goto end;

  hex:
  while (is_ip_valid(interp) &&
         ((curr(interp) >= '0' && curr(interp) <= '9') ||
          (curr(interp) >= 'a' && curr(interp) <= 'f') ||
          (curr(interp) >= 'A' && curr(interp) <= 'F'))) {
    ++interp->ip;
    has_digits = 1;
  }
  goto end;

  oct:
  while (is_ip_valid(interp) &&
         (curr(interp) >= '0' && curr(interp) <= '7')) {
    ++interp->ip;
    has_digits = 1;
  }
  goto end;

  bin:
  while (is_ip_valid(interp) &&
         (curr(interp) >= '0' && curr(interp) <= '1')) {
    ++interp->ip;
    has_digits = 1;
  }
  goto end;

  end:
  if (!has_digits) {
    print_error("Integer literal expected");
    return 0;
  }

  /* OK */
  stack_push(interp, create_string(string_data(interp->code)+begin,
                                   string_data(interp->code)+interp->ip));
  /* Move back one space if we terminated due to landing on a non-digit
   * character */
  if (is_ip_valid(interp))
    --interp->ip;
  return 1;
}

static int builtin_add(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;
  stack_push(interp, int_to_string(a+b));
  return 1;
}

static int builtin_sub(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;
  stack_push(interp, int_to_string(a-b));
  return 1;
}

static int builtin_mul(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;
  stack_push(interp, int_to_string(a*b));
  return 1;
}

static int builtin_div(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;

  if (!b) {
    print_error("Division by zero");
    return 0;
  }

  stack_push(interp, int_to_string(a/b));
  return 1;
}

static int builtin_mod(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;

  if (!b) {
    print_error("Division by zero");
    return 0;
  }

  stack_push(interp, int_to_string(a%b));
  return 1;
}

static int builtin_equal(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &a, &b)) UNDERFLOW;

  stack_push(interp, int_to_string(string_equals(a, b)));
  free(a);
  free(b);
  return 1;
}

static int builtin_notequal(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &a, &b)) UNDERFLOW;

  stack_push(interp, int_to_string(!string_equals(a, b)));
  free(a);
  free(b);
  return 1;
}

static int builtin_less(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;

  stack_push(interp, int_to_string(a < b));
  return 1;
}

static int builtin_greater(interpreter* interp) {
  signed a, b;
  if (!stack_pop_ints(interp, 2, &b, &a)) return 0;

  stack_push(interp, int_to_string(a > b));
  return 1;
}

static int builtin_stringless(interpreter* interp) {
  string a, b;
  int result;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  result = memcmp(string_data(a), string_data(b),
                  a->len < b->len? a->len : b->len);
  /* If a tie, compare lengths. */
  if (result == 0)
    result = (a->len < b->len? -1 : a->len > b->len? +1 : 0);

  stack_push(interp, int_to_string(result < 0));
  return 1;
}

static int builtin_stringgreater(interpreter* interp) {
  string a, b;
  int result;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  result = memcmp(string_data(a), string_data(b),
                  a->len < b->len? a->len : b->len);
  /* If a tie, compare lengths. */
  if (result == 0)
    result = (a->len < b->len? -1 : a->len > b->len? +1 : 0);

  stack_push(interp, int_to_string(result > 0));
  return 1;
}

static int builtin_and(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  /* Need to use non-short-circuiting operator so that the strings get freed. */
  stack_push(interp, int_to_string(string_to_bool_free(a) &
                                   string_to_bool_free(b)));
  return 1;
}

static int builtin_or(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  /* Use non-short-circuiting operator to free both strings. */
  stack_push(interp, int_to_string(string_to_bool_free(a) |
                                   string_to_bool_free(b)));
  return 1;
}

static int builtin_xor(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  stack_push(interp, int_to_string(string_to_bool_free(a) ^
                                   string_to_bool_free(b)));
  return 1;
}

static int builtin_not(interpreter* interp) {
  string a;
  if (!(a = stack_pop(interp))) UNDERFLOW;

  stack_push(interp, int_to_string(!string_to_bool_free(a)));
  return 1;
}

static int builtin_code(interpreter* interp) {
  unsigned depth = 1, begin = ++interp->ip;

  while (is_ip_valid(interp) && depth) {
    if (curr(interp) == '(') ++depth;
    if (curr(interp) == ')') --depth;
    if (depth) ++interp->ip;
  }

  /* If depth is not zero, we hit EOI. */
  if (depth) {
    print_error("Unbalanced parenthesis");
    return 0;
  }

  stack_push(interp, create_string(string_data(interp->code)+begin,
                                   string_data(interp->code)+interp->ip));
  return 1;
}

static int builtin_if(interpreter* interp) {
  string condition, then, otherwise;
  int result;

  if (!stack_pop_strings(interp, 3, &otherwise, &then, &condition)) UNDERFLOW;

  if (string_to_bool_free(condition))
    result = exec_code(interp, then);
  else
    result = exec_code(interp, otherwise);

  free(then);
  free(otherwise);
  return result;
}

static int builtin_ifs(interpreter* interp) {
  string condition, then;
  int result;

  if (!stack_pop_strings(interp, 2, &then, &condition)) UNDERFLOW;

  if (string_to_bool_free(condition))
    result = exec_code(interp, then);
  else
    result = 1;

  free(then);
  return result;
}

static int builtin_while(interpreter* interp) {
  string condition, body, s;
  int result;

  if (!stack_pop_strings(interp, 2, &body, &condition)) UNDERFLOW;

  while (1) {
    result = exec_code(interp, condition);
    if (!result) break;
    if (!(s = stack_pop(interp))) {
      print_error("Stack underflow after evaluating condition");
      result = 0;
      break;
    }
    if (!string_to_bool_free(s)) break;

    result = exec_code(interp, body);
    if (!result) break;
  }

  free(condition);
  free(body);
  return result;
}

static int builtin_whiles(interpreter* interp) {
  string body, s;
  int result;

  if (!(body = stack_pop(interp))) UNDERFLOW;

  do {
    result = exec_code(interp, body);
    if (!result) break;
    if (!(s = stack_pop(interp))) {
      print_error("Stack underflow after evaluating body");
      result = 0;
      break;
    }
    if (!string_to_bool_free(s)) break;
  } while (1);

  free(body);
  return result;
}

static int builtin_for(interpreter* interp) {
  string sreg, sfrom, sto, body;
  byte reg;
  signed from, to, inc, i;
  int result = 1;

  if (!stack_pop_strings(interp, 4, &body, &sto, &sfrom, &sreg)) UNDERFLOW;

  if (sreg->len != 1) {
    print_error_s("Invalid register", sreg);
    goto error;
  }
  reg = string_data(sreg)[0];

  if (!string_to_int(sfrom, &from)) {
    print_error_s("Invalid integer", sfrom);
    goto error;
  }

  if (!string_to_int(sto, &to)) {
    print_error_s("Invalid integer", sto);
    goto error;
  }

  inc = to > from? 1 : -1;

  for (i = 0; (inc > 0? i < to : i > to); i += inc) {
    free(interp->registers[reg]);
    interp->registers[reg] = int_to_string(i);
    result = exec_code(interp, body);
    if (!result) break;
    /* Gracefully handle alterations to the register */
    if (!string_to_int(interp->registers[reg], &i)) {
      print_error_s("Register altered to invalid integer",
                    interp->registers[reg]);
      result = 0;
      break;
    }
  }

  /* Done, clean up and return result. */
  touch_reg(interp, reg);
  free(sreg);
  free(sfrom);
  free(sto);
  free(body);
  return result;

  error:
  /* Restore stack and return failure */
  stack_push(interp, sreg);
  stack_push(interp, sfrom);
  stack_push(interp, sto);
  stack_push(interp, body);
  return 0;
}

static int builtin_fors(interpreter* interp) {
  string sto, body;
  byte reg = 'i';
  signed to, inc, i;
  int result = 1;

  if (!stack_pop_strings(interp, 2, &body, &sto)) UNDERFLOW;

  if (!string_to_int(sto, &to)) {
    print_error_s("Invavid integer", sto);
    /* Restore the stack and return failure */
    stack_push(interp, sto);
    stack_push(interp, body);
    return 0;
  }

  inc = to >= 0? +1 : -1;

  for (i = 0; (inc > 0? i < to : i > to); i += inc) {
    free(interp->registers[reg]);
    interp->registers[reg] = int_to_string(i);
    result = exec_code(interp, body);
    if (!result) break;

    if (!string_to_int(interp->registers[reg], &i)) {
      print_error_s("Register altered to invalid integer",
                    interp->registers[reg]);
      result = 0;
      break;
    }
  }

  /* Done, clean up and return */
  touch_reg(interp, reg);
  free(sto);
  free(body);
  return result;
}

static int builtin_defun(interpreter* interp) {
  string name, body;
  byte n1;
  long_command* curr;

  if (!stack_pop_strings(interp, 2, &body, &name)) UNDERFLOW;

  /* If name is one character, it is a short name; if it is more than one
   * character, it is a long name. An empty string is an error.
   */
  if (!name->len) {
    print_error("Empty command name");
    goto error;
  }

  if (name->len == 1) {
    n1 = string_data(name)[0];
    /* Both pointers are in the same spot, so check either */
    if (interp->commands[n1].cmd.native) {
      print_error_s("Short command already exists", name);
      goto error;
    } else {
      /* OK */
      interp->commands[n1].is_native = 0;
      interp->commands[n1].cmd.user = body;
      free(name);
    }
  } else {
    /* Long command name.
     * First, check to see if it already exists.
     */
    for (curr = interp->long_commands; curr; curr = curr->next) {
      if (string_equals(name, curr->name)) {
        print_error_s("Long command already exists", name);
        goto error;
      }
    }

    /* OK, add it */
    curr = tmalloc(sizeof(long_command));
    curr->name = name;
    curr->cmd.is_native = 0;
    curr->cmd.cmd.user = body;
    curr->next = interp->long_commands;
    interp->long_commands = curr;
  }

  return 1;

  error:
  /* Restore the stack and return failure */
  stack_push(interp, name);
  stack_push(interp, body);
  return 0;
}

static int builtin_contextualdefun(interpreter* interp) {
  string a, b;
  if (interp->context_active)
    return builtin_defun(interp);
  else {
    /* Pop unused strings */
    if (!stack_pop_strings(interp, 2, &a, &b)) UNDERFLOW;
    free(a);
    free(b);
    return 1;
  }
}

static int builtin_read(interpreter* interp) {
  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Register name expected");
    return 0;
  }

  stack_push(interp, dupe_string(interp->registers[curr(interp)]));
  touch_reg(interp, curr(interp));
  return 1;
}

static int builtin_write(interpreter* interp) {
  string val;

  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Register name expected");
    return 0;
  }

  if (!(val = stack_pop(interp))) UNDERFLOW;

  free(interp->registers[curr(interp)]);
  interp->registers[curr(interp)] = dupe_string(val);
  touch_reg(interp, curr(interp));
  return 1;
}

static int builtin_eval(interpreter* interp) {
  string code;

  if (!(code = stack_pop(interp))) UNDERFLOW;

  return exec_code(interp, code);
}

static int builtin_stash(interpreter* interp) {
  unsigned i;
  pstack_elt* elt;

  elt = tmalloc(sizeof(pstack_elt));
  for (i = 0; i < 256; ++i)
    elt->registers[i] = dupe_string(interp->registers[i]);
  elt->next = interp->pstack;
  interp->pstack = elt;

  return 1;
}

static int builtin_retrieve(interpreter* interp) {
  unsigned i;
  pstack_elt* top;

  if (!interp->pstack) {
    print_error("P-stack underflow");
    return 0;
  }

  top = interp->pstack;
  interp->pstack = top->next;

  /* Free current registers */
  for (i = 0; i < 256; ++i)
    free(interp->registers[i]);

  /* Restore old values */
  memcpy(interp->registers, top->registers, sizeof(interp->registers));
  free(top);
  return 1;
}

static int builtin_stashretrieve(interpreter* interp) {
  string s;
  if (!(s = stack_pop(interp))) UNDERFLOW;

  stack_push(interp,
             append_cstr(append_string(convert_string("p"), s), "P"));
  return 1;
}

static int builtin_escape(interpreter* interp) {
  byte what, x0, x1;
  string s;
  ++interp->ip;
  if (!is_ip_valid(interp)) {
    print_error("Escaped character expected");
    return 0;
  }

  switch (curr(interp)) {
  case 'a': what = '\a'; break;
  case 'b': what = '\b'; break;
  case 'e': what = '\033'; break;
  case 'f': what = '\f'; break;
  case 'n': what = '\n'; break;
  case 'r': what = '\r'; break;
  case 't': what = '\t'; break;
  case 'v': what = '\v'; break;
  case '"':
  case '\\':
  case '\'':
  case '$':
  case '%':
  case '`': what = curr(interp); break;
  case 'x': {
    /* Extract both digits if present. */
    ++interp->ip;
    if (is_ip_valid(interp)) {
      x0 = curr(interp);
      ++interp->ip;
      if (is_ip_valid(interp))
        x1 = curr(interp);
    }

    if (!is_ip_valid(interp)) {
      print_error("Two hexits expected after \\x");
      return 0;
    }

    /* Convert integer */
    if (x0 >= '0' && x0 <= '9')
      what = (x0 - '0') << 4;
    else if (x0 >= 'a' && x0 <= 'f')
      what = (x0 + 10 - 'a') << 4;
    else if (x0 >= 'A' && x0 <= 'F')
      what = (x0 + 10 - 'A') << 4;
    else {
      print_error("First \\x hexit is invalid");
      return 0;
    }
    if (x1 >= '0' && x1 <= '9')
      what |= (x1 - '0');
    else if (x1 >= 'a' && x1 <= 'f')
      what |= (x1 + 10 - 'a');
    else if (x1 >= 'A' && x1 <= 'F')
      what |= (x1 + 10 - 'A');
    else {
      print_error("Second \\x hexit is invalid");
      return 0;
    }
  } break;
  default:
    print_error("Invalid escape sequence");
    return 0;
  }

  /* Create and push the string. */
  stack_push(interp, create_string(&what, (&what)+1));
  return 1;
}

static int builtin_string(interpreter* interp) {
  string accum, s;
  unsigned begin, end;

  accum = empty_string();
  ++interp->ip;
  while (1) {
    /* Scan for the next important character. */
    for (begin = interp->ip; is_ip_valid(interp); ++interp->ip)
      if (curr(interp) == '"' ||
          curr(interp) == '$' ||
          curr(interp) == '`' ||
          curr(interp) == '\\' ||
          curr(interp) == '%')
        break;

    /* Found important character or EOI. */
    if (!is_ip_valid(interp)) {
      print_error("Encountered end-of-input in string literal");
      goto error;
    }

    /* Append everything in-between */
    accum = append_data(accum,
                        string_data(interp->code) + begin,
                        string_data(interp->code) + interp->ip);

    switch (curr(interp)) {
    case '"': goto done;
    case '$':
      ++interp->ip;
      if (!is_ip_valid(interp)) {
        print_error("Encountered end-of-input in string literal");
        goto error;
      }
      /* Append value of this register */
      accum = append_string(accum,
                            interp->registers[curr(interp)]);
      touch_reg(interp, curr(interp));
      break;

    case '%':
      s = stack_pop(interp);
      if (!s) {
        print_error("Stack underflow");
        goto error;
      }
      accum = append_string(accum, s);
      free(s);
      break;

    case '`':
      if (!interp->initial_whitespace) {
        print_error("Initial whitespace (`) not available in this context.");
        goto error;
      }
      accum = append_string(accum, interp->initial_whitespace);
      break;

    case '\\':
      if (!builtin_escape(interp)) goto error;
      s = stack_pop(interp);
      /* Popping will always succeed if escape returned success. */
      accum = append_string(accum, s);
      free(s);
      break;
    }

    /* All the above (which don't goto elsewhere) leave the IP on the end of
     * whatever special thing they did.
     */
    ++interp->ip;
  }

  done:
  stack_push(interp, accum);
  return 1;

  error:
  diagnostic(interp, NULL);
  free(accum);
  return 0;
}

static int builtin_rand(interpreter* interp) {
  stack_push(interp, int_to_string(rand() & 0xFFFF));
  return 1;
}

static int builtin_history(interpreter* interp) {
  string soff;
  signed off;

  soff = stack_pop(interp);
  if (!soff)
    /* Assume 0 */
    off = 0;
  else
    if (!string_to_int(soff, &off)) {
      print_error("Invalid integer");
      stack_push(interp, soff);
      return 0;
    }

  off += interp->history_offset;

  if (off < 0 || off >= 0x20) {
    print_error("Invalid history offset");
    if (soff)
      stack_push(interp, soff);
    return 0;
  }

  /* OK */
  stack_push(interp, dupe_string(interp->registers[off]));
  free(soff);
  ++interp->history_offset;
  return 1;
}

static int builtin_suppresshistory(interpreter* interp) {
  interp->enable_history = 0;
  return 1;
}

/* Common function for v and V.
 *
 * aux: if non-NULL, any code to evaluate before the defun
 * defun: command to run to define the command.
 */
static int builtin_defunlibrary_common(interpreter* interp,
                                       string aux,
                                       byte defun) {
  string name, body, code;
  struct tm* now;
  time_t seconds;
  char header[256], date[64];
  FILE* out;
  unsigned i;

  if (!stack_pop_strings(interp, 2, &body, &name)) UNDERFLOW;

  /* We can't allow the name to have parentheses or the NUL character. */
  for (i = 0; i < name->len; ++i)
    if (string_data(name)[i] == '(' || string_data(name)[i] == ')' ||
        string_data(name)[i] == 0) {
      print_error_s("Invalid command name (for use with v/V)", name);
      stack_push(interp, name);
      stack_push(interp, body);
      return 0;
    }

  /* Build code */
  time(&seconds);
  now = localtime(&seconds);
  strftime(date, sizeof(date)-1, "%A, %Y.%m.%d %H:%M:%S", now);
  snprintf(header, sizeof(header), "\n(Added by %s on %s);\n",
           getenv("USER"), date);
  code = convert_string(header);
  if (aux)
    code = append_string(code, aux);
  code = append_cstr(code, "(");
  code = append_string(code, name);
  code = append_cstr(code, ")(");
  code = append_string(code, body);
  code = append_cstr(code, ")");
  code = append_data(code, &defun, (&defun)+1);
  code = append_cstr(code, "\n");

  /* Evaluate in current */
  if (!exec_code(interp, code)) {
    print_error("not adding function to library due to error(s)");
    stack_push(interp, name);
    stack_push(interp, code);
    return 0;
  }

  /* Don't need name or code anymore. */
  free(name);
  free(body);

  /* OK, write out */
  out = fopen(user_library_file, "a");
  if (!out) {
    fprintf(stderr, "tgl: error: unable to open %s: %s\n",
            user_library_file, strerror(errno));
    free(code);
    return 0;
  }
  if (code->len !=
      fwrite(string_data(code), 1, code->len, out)) {
    fprintf(stderr, "tgl: error writing to %s: %s\n",
            user_library_file, strerror(errno));
    free(code);
    fclose(out);
    return 0;
  }

  /* Success */
  fclose(out);
  free(code);
  return 1;
}

static int builtin_defunlibrary(interpreter* interp) {
  return builtin_defunlibrary_common(interp, NULL, 'd');
}

struct builtins_t builtins_[] = {
  { 'Q', builtin_long_command },
  { '\'',builtin_char },
  { '.', builtin_print },
  { ':', builtin_dupe },
  { ';', builtin_drop },
  { 'x', builtin_swap },
  { 'c', builtin_concat },
  { 'l', builtin_length },
  { 'C', builtin_charat },
  { 's', builtin_substr },
  { 'S', builtin_suffix },
  { 'm', builtin_map },
  { '0', builtin_number },
  { '1', builtin_number },
  { '2', builtin_number },
  { '3', builtin_number },
  { '4', builtin_number },
  { '5', builtin_number },
  { '6', builtin_number },
  { '7', builtin_number },
  { '8', builtin_number },
  { '9', builtin_number },
  { '#', builtin_number },
  { '+', builtin_add },
  { '-', builtin_sub },
  { '*', builtin_mul },
  { '/', builtin_div },
  { '%', builtin_mod },
  { '=', builtin_equal },
  { '!', builtin_notequal },
  { '<', builtin_less },
  { '>', builtin_greater },
  { '{', builtin_stringless },
  { '}', builtin_stringgreater },
  { '&', builtin_and },
  { '|', builtin_or },
  { '^', builtin_xor },
  { '~', builtin_not },
  { '(', builtin_code },
  { 'i', builtin_if },
  { 'I', builtin_ifs },
  { 'w', builtin_while },
  { 'W', builtin_whiles },
  { 'f', builtin_for },
  { 'F', builtin_fors },
  { 'd', builtin_defun },
  { 'D', builtin_contextualdefun },
  { 'r', builtin_read },
  { 'R', builtin_write },
  { 'X', builtin_eval },
  { 'p', builtin_stash },
  { 'P', builtin_retrieve },
  { 'z', builtin_stashretrieve },
  { '\\',builtin_escape },
  { '"', builtin_string },
  { '?', builtin_rand },
  { 'h', builtin_history },
  { 'H', builtin_suppresshistory },
  { 'v', builtin_defunlibrary },
  { 0, 0 },
}, * builtins = builtins_;
/* END: Built-in commands */

/* BEGIN: Persistence */

/* Struct to head each register in the register persistence file. */
typedef struct persistent_register {
  time_t access_time;
  unsigned length;
} persistent_register;

/* Magic bytes at the beginning of the register persistence file. */
static byte register_persistence_magic[8] = {
  'T', 'g', 'l', 'V', sizeof(persistent_register), 0, 0, 0,
};

/* The format of the register persistence file is as follows:
 *   8 bytes: TglV<size of persistent_register> 0 0 0
 *     The magic header indicates the type of the file and the size of the
 *     persistent_register struct. Both are used on reading to make sure that
 *     the file is compatible.
 *   struct persistent_register { access_time = 1, length = 2 }
 *     This is used on reading to ensure that the layout of persistent_register
 *     matches what the program actually uses.
 *   256 times:
 *     struct persistent_register
 *     byte[persistent_register.length]
 *       Stores the data for each of the 256 registers, in order.
 */

/* Reads persistent registers from the given file.
 *
 * Returns 1 on success, 0 on errors. If the file is valid but truncated, the
 * registers that could be read will have been altered.
 *
 * It is not an error if the file does not exist.
 */
static int read_persistent_registers(interpreter* interp, char* filename) {
  byte magic[sizeof(register_persistence_magic)];
  persistent_register header;
  string s;
  FILE* file;
  unsigned i;

  file = fopen(filename, "rb");
  if (!file) {
    if (errno == ENOENT) {
      /* File does not exist, not an error to us. */
      return 1;
    } else {
      fprintf(stderr, "tgl: error reading register persistence file: %s\n",
              strerror(errno));
      return 0;
    }
  }

  /* Read and check magic */
  if (!fread(magic, sizeof(magic), 1, file)) {
    fprintf(stderr, "tgl: error reading register persistence file: %s\n",
            strerror(errno));
    fclose(file);
    return 0;
  }

  if (memcmp(magic, register_persistence_magic, sizeof(magic))) {
    fclose(file);
    fprintf(stderr, "tgl: register persistence file %s is incompatible\n",
            filename);
    return 0;
  }

  /* Read and check format */
  if (!fread(&header, sizeof(header), 1, file)) {
    fprintf(stderr, "tgl: error reading register persistence file: %s\n",
            strerror(errno));
    fclose(file);
    return 0;
  }

  if (header.access_time != 1 || header.length != 2) {
    fclose(file);
    fprintf(stderr, "tgl: register persistence file %s is incompatible\n",
            filename);
    return 0;
  }

  /* Read each register */
  for (i = 0; i < 256; ++i) {
    if (!fread(&header, sizeof(header), 1, file)) {
      fprintf(stderr, "tgl: error reading register persistence file: %s\n",
              strerror(errno));
      fclose(file);
      return 0;
    }

    /* Try to allocate the string, but handle allocation failure gracefully.
     * (It is conceivable we could encounter a doctored file with a really
     * large length.)
     */
    s = malloc(sizeof(struct string) + header.length);
    if (!s) {
      fprintf(stderr,
              "tgl: memory allocation for persistent register failed\n");
      fclose(file);
      return 0;
    }

    /* Set the length and read the payload in */
    s->len = header.length;
    if (s->len > 0) {
      if (s->len != fread(string_data(s), 1, s->len, file)) {
        fprintf(stderr, "tgl: error reading register persistence file: %s\n",
                strerror(errno));
        free(s);
        fclose(file);
        return 0;
      }
    }

    /* Save the register */
    free(interp->registers[i]);
    interp->registers[i] = s;
    interp->reg_access[i] = header.access_time;
  }

  /* Successful */
  fclose(file);
  return 1;
}

/* Writes persistent registers to the given file.
 *
 * Returns 1 on success, 0 on error.
 */
static int write_persistent_registers(interpreter* interp, char* filename) {
  persistent_register header;
  FILE* file;
  unsigned i;

  file = fopen(filename, "w");
  if (!file) goto error;

  if (!fwrite(register_persistence_magic,
              sizeof(register_persistence_magic), 1, file))
    goto error;

  header.access_time = 1;
  header.length = 2;
  if (!fwrite(&header, sizeof(header), 1, file)) goto error;

  for (i = 0; i < 256; ++i) {
    header.access_time = interp->reg_access[i];
    header.length = interp->registers[i]->len;
    if (!fwrite(&header, sizeof(header), 1, file)) goto error;
    if (interp->registers[i]->len !=
        fwrite(string_data(interp->registers[i]), 1,
               interp->registers[i]->len, file))
      goto error;
  }

  /* Success */
  fclose(file);
  return 1;

  error:
  fprintf(stderr, "tgl: error writing register persistence file: %s\n",
          strerror(errno));
  if (file)
    fclose(file);
  return 0;
}

/* END: Persistence */

/* Reads all text from the given file, then executes it.
 *
 * If scan_initial_whitespace is non-zero, the leading whitespace characters
 * are accumulated and stored in interpreter::initial_whitespace.
 *
 * If enable_history is non-zero, the interpreter's enable_history attribute
 * will be set and honoured when it finishes successfully.
 *
 * Returns an exit code.
 */
static int exec_file(interpreter* interp, FILE* file,
                     int scan_initial_whitespace,
                     int enable_history) {
  string input;
  char buffer[1024];
  unsigned len, i;
  int status = 0;

  interp->enable_history = enable_history;

  input = empty_string();
  while (!feof(file) && !ferror(file)) {
    len = fread(buffer, 1, sizeof(buffer), file);
    input = append_data(input, buffer, buffer+len);
  }

  if (ferror(file)) {
    perror("fread");
    free(input);
    return EXIT_IO_ERROR;
  }

  if (scan_initial_whitespace) {
    for (i=0; i < input->len && isspace(string_data(input)[i]); ++i);
    if (interp->initial_whitespace)
      free(interp->initial_whitespace);
    interp->initial_whitespace = create_string(string_data(input),
                                               string_data(input)+i);
  }
  if (!exec_code(interp, input))
    status = EXIT_PROGRAM_ERROR;

  /* Add to history if appropriate */
  if (enable_history && interp->enable_history && status == 0) {
    /* Exclude the command sequence "hX" from history. */
    for (i = 0; i < input->len && isspace(string_data(input)[i]); ++i);
    if (i < input->len && 'h' == string_data(input)[i]) {
      for (++i; i < input->len && isspace(string_data(input)[i]); ++i);
      if (i < input->len && 'X' == string_data(input)[i]) {
        for (++i; i < input->len && isspace(string_data(input)[i]); ++i);
        if (i == input->len) enable_history = 0;
      }
    }

    if (enable_history) {
      /* Shift registers 0..30 back */
      free(interp->registers[0x1F]);
      memmove(interp->registers+1, interp->registers, 0x1F*sizeof(string));
      memmove(interp->reg_access+1, interp->reg_access, 0x1F*sizeof(time_t));
      /* Log new history */
      interp->registers[0] = dupe_string(input);
      touch_reg(interp, 0);
    }
  }

  free(input);
  return status;
}

/* Opens and executes the user library, then clears the stack. */
static void load_user_library(interpreter* interp) {
  FILE* file;
  int status;

  file = fopen(user_library_file, "r");
  if (!file) {
    /* If the file doesn't exist, ignore silently; otherwise, print a
     * diagnostic.
     */
    if (errno != ENOENT)
      fprintf(stderr, "tgl: unable to open user library: %s\n",
              strerror(errno));
    return;
  }

  status = exec_file(interp, file, 0, 0);
  fclose(file);

  /* Clear the stack and reset history offset */
  while (interp->stack)
    free(stack_pop(interp));
  interp->history_offset = 0;

  /* Print notice about error in the user library if any occurred */
  if (status)
    fprintf(stderr, "tgl: error occurred in user library\n");
}

static void print_usage() {
  printf("Usage: tgl [options] [infile]\nText Generation Language\n\n");
#ifdef _GNU_SOURCE
  printf(
"  -l, --library file               Use the given file (instead of ~/.tgl)\n"
"                                   for the user library.\n"
"  -r, --register-persistence file  Use the given file (instead of\n"
"                                   ~/.tgl_registers) to preserve registers.\n"
"  -h, --help                       This help message.\n"
    );
#else
  printf(
"  -l file  Use the given file (instead of ~/.tgl) for the user library.\n"
"  -r file  Use the given file (instead of ~/.tgl_registers) to preserve\n"
"           registers.\n"
"  -h       This help message.\n"
    );
#endif /* _GNU_SOURCE */
}

int main(int argc, char** argv) {
  interpreter interp;
  char reg_persistence_file_default[256];
  char user_library_file_default[256];
  char* reg_persistence_file;
  int ret, cmdstat;
  FILE* input;
  static char short_options[] = "l:r:h";
#ifdef _GNU_SOURCE
  static struct option long_options[] = {
   { "library", 1, NULL, 'l' },
   { "register-persistence", 1, NULL, 'r' },
   { "help", 0, NULL, 'h' },
   {0},
  };
#endif /* _GNU_SOURCE */

  /* Init default file names and options */
  snprintf(reg_persistence_file_default,
           sizeof(reg_persistence_file_default),
           "%s/.tgl_registers",
           getenv("HOME"));
  snprintf(user_library_file_default,
           sizeof(user_library_file_default),
           "%s/.tgl",
           getenv("HOME"));
  user_library_file = user_library_file_default;
  reg_persistence_file = reg_persistence_file_default;
  input = stdin;

  /* Parse command-line arguments */
  do {
#ifdef _GNU_SOURCE
    cmdstat = getopt_long(argc, argv, short_options, long_options, NULL);
#else
    cmdstat = getopt(argc, argv, short_options);
#endif /* not _GNU_SOURCE */
    switch (cmdstat) {
    case -1: break;
    case 'h':
    case '?':
      print_usage();
      return EXIT_HELP;

    case 'l':
      user_library_file = optarg;
      break;

    case 'r':
      reg_persistence_file = optarg;
      break;
    }
  } while (cmdstat != -1);

  /* There should be one or zero args left. */
  if (argc - optind > 1) {
    fprintf(stderr, "tgl: too many arguments\n");
    print_usage();
    return EXIT_HELP;
  }

  if (argc - optind) {
    input = fopen(argv[optind], "r");
    if (!input) {
      fprintf(stderr, "tgl: unable to open %s: %s\n",
              argv[optind], strerror(errno));
      return EXIT_IO_ERROR;
    }
  }

  srand(time(NULL));
  interp_init(&interp);

  /* Read persistent registers */
  read_persistent_registers(&interp, reg_persistence_file_default);
  /* Try to execute the user library */
  load_user_library(&interp);
  /* Execute primary input */
  ret = exec_file(&interp, stdin, 1, 1);
  /* If successful, save registers */
  if (ret == 0)
    write_persistent_registers(&interp, reg_persistence_file_default);
  /* Done, return status to the OS */
  return ret;
}
