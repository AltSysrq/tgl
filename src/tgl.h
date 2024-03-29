/* Global variables and important functions for TGL */
#ifndef TGL_H_
#define TGL_H_

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#define EXIT_PROGRAM_ERROR 1
#define EXIT_PLATFORM_ERROR 253
#define EXIT_IO_ERROR 254
#define EXIT_OUT_OF_MEMORY 255
#define EXIT_HELP 2

/* Globals */
/* The name of the user library file. */
extern char* user_library_file;
/* The name of the current context. */
extern char* current_context;
/* If set to true, don't emit a warning when architecture detection fails. */
extern int suppress_unknown_alignment_warning;

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

#endif /* TGL_H_ */
