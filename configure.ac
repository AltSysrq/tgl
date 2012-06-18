#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.6])
AC_INIT([TGL], [1.0.0], [jasonlingle@yahoo.com])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/tgl.c])
AC_CONFIG_HEADERS([config.h])
AM_SILENT_RULES(yes)

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_SIZE_T

# Checks for library functions.
# We never malloc() or realloc() with size zero, so we don't need these
# checks.
dnl AC_FUNC_MALLOC
dnl AC_FUNC_REALLOC
AC_FUNC_FORK
AC_TYPE_PID_T
AC_CHECK_FUNCS([dup2 memmove memset strerror mkstemp])
AC_CHECK_FUNCS([getopt_long], AC_DEFINE(_GNU_SOURCE, [],
                              [Use GNU Extensions if available]))

AC_CONFIG_FILES([Makefile src/Makefile doc/Makefile])
AC_OUTPUT
