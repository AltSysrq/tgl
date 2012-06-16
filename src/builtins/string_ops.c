#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../tgl.h"
#include "../strings.h"
#include "../interp.h"

/* @builtin-decl int builtin_empty_string(interpreter*) */
/* @builtin-bind { 'y', builtin_empty_string }, */
int builtin_empty_string(interpreter* interp) {
  stack_push(interp, empty_string());
  return 1;
}

/* @builtin-decl int builtin_print(interpreter*) */
/* @builtin-bind { '.', builtin_print }, */
int builtin_print(interpreter* interp) {
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

/* @builtin-decl int builtin_char(interpreter*) */
/* @builtin-bind { '\'', builtin_char }, */
int builtin_char(interpreter* interp) {
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

/* @builtin-decl int builtin_concat(interpreter*) */
/* @builtin-bind { 'c', builtin_concat }, */
int builtin_concat(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &b, &a)) UNDERFLOW;

  a = append_string(a, b);
  free(b);

  stack_push(interp, a);
  return 1;
}

/* @builtin-decl int builtin_length(interpreter*) */
/* @builtin-bind { 'l', builtin_length }, */
int builtin_length(interpreter* interp) {
  string s;
  if (!(s = stack_pop(interp))) UNDERFLOW;

  stack_push(interp, int_to_string(s->len));
  free(s);

  return 1;
}

/* @builtin-decl int builtin_charat(interpreter*) */
/* @builtin-bind { 'C', builtin_charat }, */
int builtin_charat(interpreter* interp) {
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

/* @builtin-decl int builtin_substr(interpreter*) */
/* @builtin-bind { 's', builtin_substr }, */
int builtin_substr(interpreter* interp) {
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

/* @builtin-decl int builtin_suffix(interpreter*) */
/* @builtin-bind { 'S', builtin_suffix }, */
int builtin_suffix(interpreter* interp) {
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

/* @builtin-decl int builtin_map(interpreter*) */
/* @builtin-bind { 'm', builtin_map }, */
int builtin_map(interpreter* interp) {
  string str, *mapping, sn, result;
  signed n;
  unsigned i, j, k, bufferSize, bufferIx, height;
  byte* buffer;
  stack_elt* elt;

  if (interp->u[0]) {
    /* Get the old stack height */
    if (!secondary_arg_as_int(interp->u[0], &n, 0))
      return 0;

    /* Count the current stack height */
    height = 0;
    for (elt = interp->stack; elt; elt = elt->next)
      ++height;

    n = height - n;

    if (n < 1) {
      print_error("Invalid usage of secondary argument");
      return 0;
    }

    /* Total length must be even (2n mappings) */
    if (n&1) {
      print_error("Non-even number of mappings");
      return 0;
    }

    /* Number is valid. */
    n /= 2;
    sn = NULL;
  } else {
    if (!(sn = stack_pop(interp))) UNDERFLOW;
    if (!string_to_int(sn, &n)) {
      print_error_s("Bad integer", sn);
      stack_push(interp, sn);
      return 0;
    }
  }

  if (n < 0 || n >= 65536) {
    print_error_s("Invalid mapping size", sn);
    if (sn)
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
    if (sn)
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
  if (sn)
    free(sn);
  free(str);
  free(buffer);
  free(mapping);

  stack_push(interp, result);
  reset_secondary_args(interp);
  return 1;
}

/* @builtin-decl int builtin_equal(interpreter*) */
/* @builtin-bind { '=', builtin_equal }, */
int builtin_equal(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &a, &b)) UNDERFLOW;

  stack_push(interp, int_to_string(string_equals(a, b)));
  free(a);
  free(b);
  return 1;
}

/* @builtin-decl int builtin_notequal(interpreter*) */
/* @builtin-bind { '!', builtin_notequal }, */
int builtin_notequal(interpreter* interp) {
  string a, b;
  if (!stack_pop_strings(interp, 2, &a, &b)) UNDERFLOW;

  stack_push(interp, int_to_string(!string_equals(a, b)));
  free(a);
  free(b);
  return 1;
}

/* @builtin-decl int builtin_stringless(interpreter*) */
/* @builtin-bind { '{', builtin_stringless }, */
int builtin_stringless(interpreter* interp) {
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

/* @builtin-decl int builtin_stringgreater(interpreter*) */
/* @builtin-bind { '}', builtin_stringgreater }, */
int builtin_stringgreater(interpreter* interp) {
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

/* @builtin-decl int builtin_eval(interpreter*) */
/* @builtin-bind { 'X', builtin_eval }, */
int builtin_eval(interpreter* interp) {
  string code;

  if (!(code = stack_pop(interp))) UNDERFLOW;

  return exec_code(interp, code);
}
