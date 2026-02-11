#include <stdio.h>

#if defined(__is_libk)
#include <kernel/tty.h>

/* Shell pipe output hook (defined in shell.c) */
extern int  shell_is_pipe_mode(void);
extern void shell_pipe_putchar(char c);
#endif

int putchar(int ic) {
#if defined(__is_libk)
	char c = (char) ic;
	if (shell_is_pipe_mode())
		shell_pipe_putchar(c);
	else
		terminal_write(&c, sizeof(c));
#else
	// TODO: Implement stdio and the write system call.
#endif
	return ic;
}