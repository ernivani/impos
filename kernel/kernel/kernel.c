#include <stdio.h>

#include <kernel/tty.h>

void kernel_main(void) {
	terminal_initialize();
    printf("\n");
	printf("Hello, kernel World!\n");
}