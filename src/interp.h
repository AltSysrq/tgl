/* Contains functions and structures for working with the TGL interpreter. */
#ifndef INTERP_H_
#define INTERP_H_

#include "strings.h"
#include "builtins/payload.h"

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

/* The number of secondary arguments supported. */
#define NUM_SECONDARY_ARGS 4

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

  /* The four secondary arguments. */
  string u[NUM_SECONDARY_ARGS];
  /* The current index within the above. */
  unsigned ux;

  /* The interpreter's payload data */
  payload_data payload;
} interpreter;

/* Pushes the given string onto the stack of the given interpreter.
 *
 * After this call, the caller must not free the string, as its presence on the
 * stack means it will be freed by someone else. Because of this, the same
 * string object cannot be pushed onto the stack more than once.
 */
void stack_push(interpreter*, string);

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
string stack_pop(interpreter*);

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
int stack_pop_strings(interpreter*, unsigned, ...);

/* Like stack_pop_strings(), but writes into an array instead of variadic
 * arguments.
 */
int stack_pop_array(interpreter*, unsigned, string[]);

/* Like stack_pop_strings(), but converts arguments to integers (signed*s).
 *
 * This function is not entirely atomic: If popping is successful, but
 * conversion fails, some of the integers may have been altered. In any case,
 * either the function is successful or the stack is unchanged.
 *
 * This function will produce it's own error messages on failure, since it
 * cannot indicate specifics to the caller.
 */
int stack_pop_ints(interpreter*, unsigned, ...);

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
void touch_reg(interpreter*, byte);

/* Clears the interpreter's secondary arguments. */
void reset_secondary_args(interpreter* interp);

/* Prints an error message to the user. */
void print_error(char*);

/* Like print_error, but includes the given string. */
void print_error_s(char*, string);

/* Prints a diagnostic to the user, including the given error message.
 * If the message is omitted, only context is shown.
 */
void diagnostic(interpreter*, char*);

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
int exec_one_command(interpreter*);

/* Executes the given code in the given interpreter.
 *
 * This will temprorarily alter the code and ip fields of the interpreter, but
 * they will be restored before the function returns.
 */
int exec_code(interpreter*, string);

/* The table of builtin commands */
extern struct builtins_t { char name; native_command cmd; } * builtins;

/* Initialises the given interpreter. */
void interp_init(interpreter*);

/* Frees the contents of the given interpreter.
 * (But not the interpreter itself.)
 *
 * The interpreter is invalid after calling this.
 */
void interp_destroy(interpreter*);

#endif /* INTERP_H_ */
