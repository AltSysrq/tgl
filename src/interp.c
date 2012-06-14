#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>

#include "tgl.h"
#include "strings.h"
#include "interp.h"

void stack_push(interpreter* interp, string val) {
  stack_elt* s = tmalloc(sizeof(stack_elt));
  s->value = val;
  s->next = interp->stack;
  interp->stack = s;
}
string stack_pop(interpreter* interp) {
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

int stack_pop_strings(interpreter* interp, unsigned n, ...) {
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
int stack_pop_array(interpreter* interp, unsigned n, string dst[]) {
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

int stack_pop_ints(interpreter* interp, unsigned n, ...) {
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

void touch_reg(interpreter* interp, byte reg) {
  interp->reg_access[reg] = time(0);
}

void reset_secondary_args(interpreter* interp) {
  unsigned i;

  for (i = 0; i < NUM_SECONDARY_ARGS; ++i)
    if (interp->u[i])
      free(interp->u[i]);

  memset(interp->u, 0, sizeof(interp->u));
  interp->ux = 0;
}

int secondary_arg_as_int(string str, signed* dst, int allow_negative) {
  signed cnt = 1;

  if (str) {
    if (!string_to_int(str, &cnt)) {
      print_error_s("Invalid integer", str);
      return 0;
    }

    if (!allow_negative && cnt < 0) {
      print_error_s("Invalid count", str);
      return 0;
    }
  }

  *dst = cnt;
  return 1;
}

int secondary_arg_as_reg(string str, byte* dst) {
  if (str) {
    if (str->len != 1) {
      print_error_s("Invalid register", str);
      return 0;
    }

    *dst = string_data(str)[0];
  }

  return 1;
}

void print_error(char* message) {
  fprintf(stderr, "tgl: error: %s\n", message);
}

void print_error_s(char* message, string append) {
  fprintf(stderr, "tgl: error: %s: ", message);
  fwrite(string_data(append), 1, append->len, stderr);
  fprintf(stderr, "\n");
}

void diagnostic(interpreter* interp, char* message) {
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

int exec_one_command(interpreter* interp) {
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

int exec_code(interpreter* interp, string code) {
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

void interp_init(interpreter* interp) {
  unsigned i;

  memset(interp, 0, sizeof(interpreter));
  interp->context_active = 1;

  for (i=0; i < 256; ++i)
    interp->registers[i] = empty_string();
  for (i=0; builtins[i].name; ++i) {
    interp->commands[(unsigned)builtins[i].name].is_native = 1;
    interp->commands[(unsigned)builtins[i].name].cmd.native = builtins[i].cmd;
  }

  payload_data_init(&interp->payload);
}

void interp_destroy(interpreter* interp) {
  unsigned i;
  stack_elt* currs, *nexts;
  long_command* currlc, *nextlc;
  pstack_elt* currps, *nextps;

  for (i = 0; i < 256; ++i) {
    free(interp->registers[i]);
    if (interp->commands[i].cmd.user &&
        !interp->commands[i].is_native)
      free(interp->commands[i].cmd.user);
  }

  for (currs = interp->stack; currs; currs = nexts) {
    nexts = currs->next;
    free(currs->value);
    free(currs);
  }
  for (currlc = interp->long_commands; currlc; currlc = nextlc) {
    nextlc = currlc->next;
    if (!currlc->cmd.is_native)
      free(currlc->cmd.cmd.user);
    free(currlc->name);
    free(currlc);
  }
  for (currps = interp->pstack; currps; currps = nextps) {
    nextps = currps->next;
    for (i = 0; i < 256; ++i)
      free(currps->registers[i]);
    free(currps);
  }

  if (interp->initial_whitespace)
    free(interp->initial_whitespace);

  payload_data_destroy(&interp->payload);
}
