#ifndef _KERNEL_TEST_H
#define _KERNEL_TEST_H

/* Shared counters — defined in test.c, used by all test_*.c files */
extern int test_count;
extern int test_pass;
extern int test_fail;

#define TEST_ASSERT(cond, msg) do { \
    test_count++; \
    if (!(cond)) { \
        printf("  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        test_fail++; \
    } else { \
        test_pass++; \
    } \
} while (0)

void test_run_all(void);

/* Group runners (each defined in app/test/test_*.c) */
void test_libc_all(void);
void test_fs_all(void);
void test_net_all(void);
void test_win32_all(void);
void test_process_all(void);
void test_shell_all(void);

#endif
