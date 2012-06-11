/* Main program for TGL (Text Generation Language). */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
#include <fnmatch.h>
#ifdef _GNU_SOURCE
#include <getopt.h>
#endif /* _GNU_SOURCE */

#include "tgl.h"
#include "strings.h"
#include "interp.h"

char* user_library_file, * current_context;

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
      fprintf(stderr, "tgl: register persistence file truncated\n");
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
        fprintf(stderr, "tgl: register persistence file truncated\n");
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

  /* Set all bytes to zero first so Valgrind won't complain.
   * (On AMD64, there is unused space between the fields which is
   * "uninitialised", though its contents do not matter to us, even on-disk.)
   */
  memset(&header, 0, sizeof(header));
  header.access_time = 1;
  header.length = 2;
  if (!fwrite(&header, sizeof(header), 1, file)) goto error;

  for (i = 0; i < 256; ++i) {
    header.access_time = interp->reg_access[i];
    header.length = interp->registers[i]->len;
    if (!fwrite(&header, sizeof(header), 1, file)) goto error;
    if (interp->registers[i]->len > 0)
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
 * If set_global_code is true, the global_code of the payload data is set to
 * the executing code, and reset to NULL on return.
 *
 * Returns an exit code.
 */
static int exec_file(interpreter* interp, FILE* file,
                     int scan_initial_whitespace,
                     int enable_history,
                     int set_global_code) {
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

  if (set_global_code)
    interp->payload.global_code = input;

  if (!exec_code(interp, input))
    status = EXIT_PROGRAM_ERROR;

  if (set_global_code)
    interp->payload.global_code = NULL;

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

  status = exec_file(interp, file, 0, 0, 0);
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
"  -c, --context name               Specify the current context.\n"
"  -h, --help                       This help message.\n"
    );
#else
  printf(
"  -l file  Use the given file (instead of ~/.tgl) for the user library.\n"
"  -r file  Use the given file (instead of ~/.tgl_registers) to preserve\n"
"           registers.\n"
"  -c name  Specify the current context.\n"
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
  static char short_options[] = "l:r:c:h";
#ifdef _GNU_SOURCE
  static struct option long_options[] = {
   { "library", 1, NULL, 'l' },
   { "register-persistence", 1, NULL, 'r' },
   { "context", 1, NULL, 'c' },
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
  current_context = "";
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

    case 'c':
      current_context = optarg;
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
  read_persistent_registers(&interp, reg_persistence_file);
  /* Try to execute the user library */
  load_user_library(&interp);
  /* Execute primary input */
  ret = exec_file(&interp, input, 1, 1, 1);
  /* If successful, save registers */
  if (ret == 0)
    write_persistent_registers(&interp, reg_persistence_file);
  /* Done, return status to the OS */
  interp_destroy(&interp);
  return ret;
}
