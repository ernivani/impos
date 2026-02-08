#include <kernel/test.h>
#include <kernel/fs.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int test_count;
static int test_pass;
static int test_fail;

/* ---- String Tests ---- */

static void test_string(void) {
    printf("== String Tests ==\n");

    /* strlen */
    TEST_ASSERT(strlen("") == 0, "strlen empty");
    TEST_ASSERT(strlen("hello") == 5, "strlen hello");
    TEST_ASSERT(strlen("a") == 1, "strlen single");

    /* strcmp */
    TEST_ASSERT(strcmp("abc", "abc") == 0, "strcmp equal");
    TEST_ASSERT(strcmp("abc", "abd") < 0, "strcmp less");
    TEST_ASSERT(strcmp("abd", "abc") > 0, "strcmp greater");
    TEST_ASSERT(strcmp("", "") == 0, "strcmp empty");

    /* strncmp */
    TEST_ASSERT(strncmp("abcdef", "abcxyz", 3) == 0, "strncmp equal prefix");
    TEST_ASSERT(strncmp("abcdef", "abcxyz", 4) != 0, "strncmp differ");
    TEST_ASSERT(strncmp("abc", "abc", 10) == 0, "strncmp short");

    /* strcpy */
    char buf[64];
    strcpy(buf, "test");
    TEST_ASSERT(strcmp(buf, "test") == 0, "strcpy basic");

    /* strncpy */
    memset(buf, 'X', sizeof(buf));
    strncpy(buf, "hi", 5);
    TEST_ASSERT(strcmp(buf, "hi") == 0, "strncpy basic");
    TEST_ASSERT(buf[2] == '\0' && buf[3] == '\0' && buf[4] == '\0', "strncpy pads");

    /* strcat */
    strcpy(buf, "hello");
    strcat(buf, " world");
    TEST_ASSERT(strcmp(buf, "hello world") == 0, "strcat basic");

    /* strchr */
    TEST_ASSERT(strchr("hello", 'l') != NULL, "strchr found");
    TEST_ASSERT(strchr("hello", 'l') == &"hello"[2], "strchr position");
    TEST_ASSERT(strchr("hello", 'z') == NULL, "strchr not found");

    /* strstr */
    TEST_ASSERT(strstr("hello world", "world") != NULL, "strstr found");
    TEST_ASSERT(strstr("hello world", "xyz") == NULL, "strstr not found");
    TEST_ASSERT(strstr("hello", "") != NULL, "strstr empty needle");
    TEST_ASSERT(strstr("abcabc", "cab") != NULL, "strstr overlap");

    /* memcmp */
    TEST_ASSERT(memcmp("abc", "abc", 3) == 0, "memcmp equal");
    TEST_ASSERT(memcmp("abc", "abd", 3) != 0, "memcmp differ");

    /* memcpy */
    char src[] = "data";
    char dst[8];
    memcpy(dst, src, 5);
    TEST_ASSERT(strcmp(dst, "data") == 0, "memcpy basic");

    /* memset */
    memset(buf, 'A', 5);
    buf[5] = '\0';
    TEST_ASSERT(strcmp(buf, "AAAAA") == 0, "memset basic");

    /* memmove (overlapping) */
    char overlap[] = "abcdef";
    memmove(overlap + 2, overlap, 4);
    TEST_ASSERT(memcmp(overlap, "ababcd", 6) == 0, "memmove overlap");
}

/* ---- Stdlib Tests ---- */

static void test_stdlib(void) {
    printf("== Stdlib Tests ==\n");

    /* atoi */
    TEST_ASSERT(atoi("0") == 0, "atoi zero");
    TEST_ASSERT(atoi("42") == 42, "atoi positive");
    TEST_ASSERT(atoi("-7") == -7, "atoi negative");
    TEST_ASSERT(atoi("  123") == 123, "atoi whitespace");
    TEST_ASSERT(atoi("99abc") == 99, "atoi trailing");
    TEST_ASSERT(atoi("") == 0, "atoi empty");

    /* strtol */
    char* end;
    TEST_ASSERT(strtol("255", &end, 10) == 255, "strtol decimal");
    TEST_ASSERT(*end == '\0', "strtol endptr at end");

    TEST_ASSERT(strtol("0xff", &end, 0) == 255, "strtol auto hex");
    TEST_ASSERT(strtol("077", &end, 0) == 63, "strtol auto octal");
    TEST_ASSERT(strtol("100", &end, 0) == 100, "strtol auto decimal");

    TEST_ASSERT(strtol("ff", &end, 16) == 255, "strtol hex");
    TEST_ASSERT(strtol("-10", &end, 10) == -10, "strtol negative");
    TEST_ASSERT(strtol("0xFF", &end, 16) == 255, "strtol hex prefix");

    /* malloc/free */
    void* p1 = malloc(32);
    TEST_ASSERT(p1 != NULL, "malloc returns non-null");

    void* p2 = malloc(64);
    TEST_ASSERT(p2 != NULL, "malloc second alloc");
    TEST_ASSERT(p1 != p2, "malloc different addresses");

    /* Write and read back */
    memset(p1, 0xAA, 32);
    memset(p2, 0xBB, 64);
    TEST_ASSERT(((unsigned char*)p1)[0] == 0xAA, "malloc write p1");
    TEST_ASSERT(((unsigned char*)p2)[0] == 0xBB, "malloc write p2");

    free(p1);
    free(p2);

    /* Malloc after free should reuse */
    void* p3 = malloc(16);
    TEST_ASSERT(p3 != NULL, "malloc after free");
    free(p3);
}

/* ---- snprintf Tests ---- */

static void test_snprintf(void) {
    printf("== snprintf Tests ==\n");

    char buf[128];

    snprintf(buf, sizeof(buf), "hello");
    TEST_ASSERT(strcmp(buf, "hello") == 0, "snprintf plain string");

    snprintf(buf, sizeof(buf), "%d", 42);
    TEST_ASSERT(strcmp(buf, "42") == 0, "snprintf %%d positive");

    snprintf(buf, sizeof(buf), "%d", -7);
    TEST_ASSERT(strcmp(buf, "-7") == 0, "snprintf %%d negative");

    snprintf(buf, sizeof(buf), "%d", 0);
    TEST_ASSERT(strcmp(buf, "0") == 0, "snprintf %%d zero");

    snprintf(buf, sizeof(buf), "%s", "world");
    TEST_ASSERT(strcmp(buf, "world") == 0, "snprintf %%s");

    snprintf(buf, sizeof(buf), "%x", 255);
    TEST_ASSERT(strcmp(buf, "ff") == 0, "snprintf %%x");

    snprintf(buf, sizeof(buf), "%u", 12345);
    TEST_ASSERT(strcmp(buf, "12345") == 0, "snprintf %%u");

    snprintf(buf, sizeof(buf), "%c", 'A');
    TEST_ASSERT(strcmp(buf, "A") == 0, "snprintf %%c");

    snprintf(buf, sizeof(buf), "%s=%d", "x", 5);
    TEST_ASSERT(strcmp(buf, "x=5") == 0, "snprintf mixed");

    /* Truncation */
    snprintf(buf, 4, "hello");
    TEST_ASSERT(strcmp(buf, "hel") == 0, "snprintf truncation");
}

/* ---- Filesystem Tests ---- */

static void test_fs(void) {
    printf("== Filesystem Tests ==\n");

    /* Create and read back a file */
    int ret = fs_create_file("/tmp_test_file", 0);
    TEST_ASSERT(ret == 0, "fs create file");

    const char* data = "test data 123";
    ret = fs_write_file("/tmp_test_file", (const uint8_t*)data, strlen(data));
    TEST_ASSERT(ret == 0, "fs write file");

    uint8_t rbuf[512];
    size_t rsize;
    ret = fs_read_file("/tmp_test_file", rbuf, &rsize);
    TEST_ASSERT(ret == 0, "fs read file");
    TEST_ASSERT(rsize == strlen(data), "fs read size matches");
    TEST_ASSERT(memcmp(rbuf, data, rsize) == 0, "fs read data matches");

    /* Delete file */
    ret = fs_delete_file("/tmp_test_file");
    TEST_ASSERT(ret == 0, "fs delete file");

    /* Verify deleted */
    ret = fs_read_file("/tmp_test_file", rbuf, &rsize);
    TEST_ASSERT(ret != 0, "fs deleted file unreadable");

    /* mkdir */
    ret = fs_create_file("/tmp_test_dir", 1);
    TEST_ASSERT(ret == 0, "fs mkdir");

    ret = fs_delete_file("/tmp_test_dir");
    TEST_ASSERT(ret == 0, "fs rmdir");

    /* Symlink */
    fs_create_file("/tmp_sym_target", 0);
    fs_write_file("/tmp_sym_target", (const uint8_t*)"symdata", 7);

    ret = fs_create_symlink("/tmp_sym_target", "/tmp_sym_link");
    TEST_ASSERT(ret == 0, "fs create symlink");

    char linkbuf[256];
    ret = fs_readlink("/tmp_sym_link", linkbuf, sizeof(linkbuf));
    TEST_ASSERT(ret == 0, "fs readlink");
    TEST_ASSERT(strcmp(linkbuf, "/tmp_sym_target") == 0, "fs readlink target");

    /* Read through symlink */
    ret = fs_read_file("/tmp_sym_link", rbuf, &rsize);
    TEST_ASSERT(ret == 0, "fs read through symlink");
    TEST_ASSERT(rsize == 7, "fs symlink read size");
    TEST_ASSERT(memcmp(rbuf, "symdata", 7) == 0, "fs symlink read data");

    /* Cleanup */
    fs_delete_file("/tmp_sym_link");
    fs_delete_file("/tmp_sym_target");

    /* Permission test - chmod */
    fs_create_file("/tmp_perm_test", 0);
    ret = fs_chmod("/tmp_perm_test", 0644);
    TEST_ASSERT(ret == 0, "fs chmod");
    fs_delete_file("/tmp_perm_test");
}

/* ---- User Tests ---- */

static void test_user(void) {
    printf("== User Tests ==\n");

    const char* name = user_get_current();
    TEST_ASSERT(name != NULL, "current user not null");

    uint16_t uid = user_get_current_uid();
    TEST_ASSERT(uid != 65535, "current uid valid");

    user_t* u = user_get(name);
    TEST_ASSERT(u != NULL, "user_get current");
    if (u) {
        TEST_ASSERT(u->uid == uid, "uid matches");
    }

    /* Group membership */
    uint16_t gid = user_get_current_gid();
    TEST_ASSERT(gid != 65535, "current gid valid");
}

/* ---- Run All ---- */

void test_run_all(void) {
    test_count = 0;
    test_pass = 0;
    test_fail = 0;

    printf("\n=== ImposOS Regression Tests ===\n\n");

    test_string();
    test_stdlib();
    test_snprintf();
    test_fs();
    test_user();

    printf("\n=== Results: %d/%d passed", test_pass, test_count);
    if (test_fail > 0) {
        printf(", %d FAILED", test_fail);
    }
    printf(" ===\n\n");
}
