#include <kernel/test.h>
#include <stdio.h>

/* Shared counters — used by all test_*.c files via extern in test.h */
int test_count;
int test_pass;
int test_fail;

void test_run_all(void) {
    test_count = 0;
    test_pass = 0;
    test_fail = 0;

    printf("\n=== ImposOS Regression Tests ===\n\n");

    test_libc_all();
    test_fs_all();
    test_net_all();
    test_win32_all();
    test_process_all();
    test_shell_all();

    printf("\n=== Results: %d/%d passed", test_pass, test_count);
    if (test_fail > 0) {
        printf(", %d FAILED", test_fail);
    }
    printf(" ===\n\n");
}
