bin_PROGRAMS = tgl
BUILTIN_FILES = \
 builtins/context.c\
 builtins/logical_ops.c\
 builtins/long_command.c\
 builtins/registers.c\
 builtins/math_ops.c\
 builtins/defun.c\
 builtins/ctrl_for.c\
 builtins/quoting.c\
 builtins/ctrl_while.c\
 builtins/ctrl_if.c\
 builtins/history.c\
 builtins/stack_ops.c\
 builtins/string_ops.c\
 builtins/payload.c\
 builtins/secarg.c\
 builtins/external.c

tgl_SOURCES = tgl.c strings.c interp.c builtins.c $(BUILTIN_FILES)

builtins.c: $(BUILTIN_FILES)
	./generate_builtins_c
