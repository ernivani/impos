#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#define ATEXIT_MAX 32

static void (*atexit_funcs[ATEXIT_MAX])(void);
static int atexit_count = 0;

int atexit(void (*function)(void)) {
	if (function == NULL || atexit_count >= ATEXIT_MAX)
		return -1;
	atexit_funcs[atexit_count++] = function;
	return 0;
}

__attribute__((__noreturn__))
void exit(int status) {
	/* Call atexit handlers in reverse order of registration */
	for (int i = atexit_count - 1; i >= 0; i--) {
		if (atexit_funcs[i] != NULL)
			atexit_funcs[i]();
	}

	/* Flush and close all open stdio streams */
	/* In kernel mode, we don't have file streams to close,
	 * but we could flush any pending output here if needed */

#if defined(__is_libk)
	if (status != EXIT_SUCCESS)
		printf("Process terminated with status %d\n", status);
	else
		printf("Process terminated successfully\n");
#else
	/* In user mode, would perform system call to terminate process */
	(void)status;
#endif

	/* Disable interrupts and halt */
	asm volatile("cli");
	while (1)
		asm volatile("hlt");
	__builtin_unreachable();
}
