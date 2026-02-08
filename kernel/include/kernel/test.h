#ifndef _KERNEL_TEST_H
#define _KERNEL_TEST_H

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

#endif
