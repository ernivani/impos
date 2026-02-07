#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#define ATEXIT_MAX 32

static void (*atexit_funcs[ATEXIT_MAX])(void);
static int atexit_count = 0;
static jmp_buf *restart_env = NULL;

int atexit(void (*function)(void)) {
	if (function == NULL || atexit_count >= ATEXIT_MAX)
		return -1;
	atexit_funcs[atexit_count++] = function;
	return 0;
}

void exit_set_restart_point(jmp_buf *env) {
	restart_env = env;
}

__attribute__((__noreturn__))
void exit(int status) {
	/* Call atexit handlers in reverse order of registration */
	for (int i = atexit_count - 1; i >= 0; i--) {
		if (atexit_funcs[i] != NULL)
			atexit_funcs[i]();
	}

	atexit_count = 0;

#if defined(__is_libk)
	if (restart_env != NULL) {
		if (status != EXIT_SUCCESS)
			printf("Shell exited with status %d\n", status);
		longjmp(*restart_env, 1);
	}
	
	if (status != EXIT_SUCCESS)
		printf("System halted with status %d\n", status);
	else
		printf("System halted.\n");
#else
	(void)status;
#endif

	asm volatile("cli");
	while (1)
		asm volatile("hlt");
	__builtin_unreachable();
}
