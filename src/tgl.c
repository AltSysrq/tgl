/* Main program for TGL (Text Generation Language). */

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

/* Represents a single stack element. */
typedef struct stack_elt {
  /* The datum held in this element. */
  string value;
  /* The next item in the stack, or NULL if this is the bottom. */
  struct stack_elt* next;
} stack_elt;

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
  int isNative;
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
  /* The stack, initially NULL. */
  stack_elt* stack;
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
 */
static int stack_pop_ints(interpreter* interp, unsigned n, ...) {
  string values[n];
  va_list args;
  unsigned i;

  if (!stack_pop_array(interp, n, values))
    return 0;

  /* Try to convert them */
  va_start(args, n);
  for (i = 0; i < n; ++i) {
    if (!string_to_int(values[i], va_arg(args, signed*))) {
      /* Conversion failed, push the values back onto the stack and return
       * failure.
       */
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

/* Prints an error message to the user. */
static void print_error(char* message) {
  fprintf(stderr, "tgl: Error: %s\n", message);
}

/* Prints a diagnostic to the user, including the given error message.
 * If the message is omitted, only context is shown.
 */
static void diagnostic(interpreter* interp, char* message) {
  char context[33];
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
  fprintf(stderr, "While executing:\n\t%s\n\t%*s\n",
          context, interp->ip-offset+1, "^");
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
 */
static int exec_one_command(interpreter* interp) {
  byte command;
  int success;
  /* Skip whitespace */
  while (interp->ip < interp->code->len &&
         isspace(string_data(interp->code)[interp->ip]))
    ++interp->ip;

  /* Return success if nothing is left. */
  if (interp->ip >= interp->code->len)
    return 1;

  /* Ensure the command exists */
  command = string_data(interp->code)[interp->ip];
  if (!interp->commands[command].cmd.native) {
    diagnostic(interp, "No such command");
    return 0;
  }

  /* Execute the command. */
  if (interp->commands[command].isNative)
    success = interp->commands[command].cmd.native(interp);
  else
    success = exec_code(interp, interp->commands[command].cmd.user);

  /* Move to next command if successful, then return. */
  if (success)
    ++interp->ip;
  else
    /* Show a simple diagnostic; this will create a sort of stack trace in cases
     * of nested code.
     */
    diagnostic(interp, NULL);
  return success;
}

/* Executes the given code in the given interpreter.
 *
 * This will temprorarily alter the code and ip fields of the interpreter, but
 * they will be restored before the function returns.
 */
static int exec_code(interpreter* interp, string code) {
  string oldCode;
  unsigned oldIP;
  int success;

  /* Back current values up */
  oldCode = interp->code;
  oldIP = interp->ip;

  /* Set new values for execution */
  interp->code = code;
  interp->ip = 0;
  success = 1;
  /* Execute to completion or failure. */
  while (interp->ip < interp->code->len && success)
    success = exec_one_command(interp);

  /* Restore old values */
  interp->code = oldCode;
  interp->ip = oldIP;

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
    interp->commands[builtins[i].name].isNative = 1;
    interp->commands[builtins[i].name].cmd.native = builtins[i].cmd;
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
    return 1;
  }

  /* Execute, clean up, and return */
  if (curr->cmd.isNative)
    result = curr->cmd.cmd.native(interp);
  else
    result = exec_code(interp, curr->cmd.cmd.user);

  return result;
}

static int builtin_print(interpreter* interp) {
  string str;

  if (!(str = stack_pop(interp))) UNDERFLOW;

  if (1 != fwrite(string_data(str), str->len, 1, stdout)) {
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

struct builtins_t builtins_[] = {
  { 'Q', builtin_long_command },
  { '\'',builtin_char },
  { '.', builtin_print },
}, * builtins = builtins_;
/* END: Built-in commands */

int main(int argc, char** argv) {
  printf("hello world\n");
  return 0;
}
