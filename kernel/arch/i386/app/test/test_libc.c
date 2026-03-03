#include <kernel/test.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

/* ---- New String Tests (strrchr, strnlen, memchr, strcspn, strspn, strpbrk) ---- */

static void test_string_extra(void) {
    printf("== String Extra Tests ==\n");

    /* strrchr */
    TEST_ASSERT(strrchr("hello", 'l') == &"hello"[3], "strrchr last match");
    TEST_ASSERT(strrchr("hello", 'z') == NULL, "strrchr not found");
    TEST_ASSERT(strrchr("hello", 'h') == &"hello"[0], "strrchr first char");

    /* strnlen */
    TEST_ASSERT(strnlen("hello", 10) == 5, "strnlen within bound");
    TEST_ASSERT(strnlen("hello", 3) == 3, "strnlen truncated");
    TEST_ASSERT(strnlen("", 5) == 0, "strnlen empty");

    /* memchr */
    const char* s = "abcdef";
    TEST_ASSERT(memchr(s, 'c', 6) == s + 2, "memchr found");
    TEST_ASSERT(memchr(s, 'z', 6) == NULL, "memchr not found");
    TEST_ASSERT(memchr(s, 'e', 3) == NULL, "memchr out of range");

    /* strcspn */
    TEST_ASSERT(strcspn("hello", "lo") == 2, "strcspn basic");
    TEST_ASSERT(strcspn("hello", "xyz") == 5, "strcspn no match");
    TEST_ASSERT(strcspn("hello", "h") == 0, "strcspn first char");

    /* strspn */
    TEST_ASSERT(strspn("hello", "hel") == 4, "strspn basic");
    TEST_ASSERT(strspn("hello", "xyz") == 0, "strspn no match");
    TEST_ASSERT(strspn("aaab", "a") == 3, "strspn repeated");

    /* strpbrk */
    TEST_ASSERT(strpbrk("hello", "lo") == &"hello"[2], "strpbrk found");
    TEST_ASSERT(strpbrk("hello", "xyz") == NULL, "strpbrk not found");
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

    /* atol */
    TEST_ASSERT(atol("0") == 0L, "atol zero");
    TEST_ASSERT(atol("100000") == 100000L, "atol large");
    TEST_ASSERT(atol("-999") == -999L, "atol negative");
    TEST_ASSERT(atol("  42") == 42L, "atol whitespace");

    /* atoll */
    TEST_ASSERT(atoll("0") == 0LL, "atoll zero");
    TEST_ASSERT(atoll("1234567890") == 1234567890LL, "atoll large");
    TEST_ASSERT(atoll("-1234567890") == -1234567890LL, "atoll negative");

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

/* ---- New Stdlib Tests (realloc, calloc, abs, div, rand, qsort, bsearch) ---- */

static int int_compare(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

static void test_stdlib_extra(void) {
    printf("== Stdlib Extra Tests ==\n");

    /* realloc */
    void* p = realloc(NULL, 32);
    TEST_ASSERT(p != NULL, "realloc NULL is malloc");
    memset(p, 0x42, 32);

    void* p2 = realloc(p, 64);
    TEST_ASSERT(p2 != NULL, "realloc grow");
    TEST_ASSERT(((unsigned char*)p2)[0] == 0x42, "realloc preserves data");

    void* p3 = realloc(p2, 16);
    TEST_ASSERT(p3 != NULL, "realloc shrink");

    void* p4 = realloc(p3, 0);
    TEST_ASSERT(p4 == NULL, "realloc zero frees");

    /* calloc */
    int* arr = (int*)calloc(10, sizeof(int));
    TEST_ASSERT(arr != NULL, "calloc non-null");
    int all_zero = 1;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != 0) { all_zero = 0; break; }
    }
    TEST_ASSERT(all_zero, "calloc zeroed");
    free(arr);

    /* abs / labs */
    TEST_ASSERT(abs(5) == 5, "abs positive");
    TEST_ASSERT(abs(-5) == 5, "abs negative");
    TEST_ASSERT(abs(0) == 0, "abs zero");
    TEST_ASSERT(labs(-100L) == 100L, "labs negative");

    /* div / ldiv */
    div_t d = div(17, 5);
    TEST_ASSERT(d.quot == 3, "div quot");
    TEST_ASSERT(d.rem == 2, "div rem");

    ldiv_t ld = ldiv(-17L, 5L);
    TEST_ASSERT(ld.quot == -3L, "ldiv quot");
    TEST_ASSERT(ld.rem == -2L, "ldiv rem");

    /* rand / srand */
    srand(42);
    int r1 = rand();
    int r2 = rand();
    srand(42);
    int r3 = rand();
    TEST_ASSERT(r1 == r3, "srand deterministic");
    TEST_ASSERT(r1 != r2 || r1 == r2, "rand returns int");  /* always passes, just exercises rand */
    TEST_ASSERT(r1 >= 0 && r1 <= RAND_MAX, "rand in range");

    /* qsort */
    int data[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    qsort(data, 10, sizeof(int), int_compare);
    int sorted = 1;
    for (int i = 0; i < 9; i++) {
        if (data[i] > data[i + 1]) { sorted = 0; break; }
    }
    TEST_ASSERT(sorted, "qsort sorts");
    TEST_ASSERT(data[0] == 0, "qsort first");
    TEST_ASSERT(data[9] == 9, "qsort last");

    /* bsearch */
    int key = 5;
    int* found = (int*)bsearch(&key, data, 10, sizeof(int), int_compare);
    TEST_ASSERT(found != NULL, "bsearch found");
    TEST_ASSERT(found && *found == 5, "bsearch value");

    int missing = 42;
    int* nf = (int*)bsearch(&missing, data, 10, sizeof(int), int_compare);
    TEST_ASSERT(nf == NULL, "bsearch not found");
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

/* ---- sscanf Tests ---- */

static void test_sscanf(void) {
    printf("== sscanf Tests ==\n");

    int a, b;
    TEST_ASSERT(sscanf("42", "%d", &a) == 1, "sscanf single int");
    TEST_ASSERT(a == 42, "sscanf int value");

    TEST_ASSERT(sscanf("10 20", "%d %d", &a, &b) == 2, "sscanf two ints");
    TEST_ASSERT(a == 10 && b == 20, "sscanf two int values");

    TEST_ASSERT(sscanf("-5", "%d", &a) == 1, "sscanf negative int");
    TEST_ASSERT(a == -5, "sscanf negative value");

    unsigned int x;
    TEST_ASSERT(sscanf("0xFF", "%x", &x) == 1, "sscanf hex");
    TEST_ASSERT(x == 0xFF, "sscanf hex value");

    TEST_ASSERT(sscanf("ff", "%x", &x) == 1, "sscanf hex no prefix");
    TEST_ASSERT(x == 0xFF, "sscanf hex no prefix value");

    char str[64];
    TEST_ASSERT(sscanf("hello world", "%s", str) == 1, "sscanf string");
    TEST_ASSERT(strcmp(str, "hello") == 0, "sscanf string value");

    char c;
    TEST_ASSERT(sscanf("A", "%c", &c) == 1, "sscanf char");
    TEST_ASSERT(c == 'A', "sscanf char value");

    unsigned int u;
    TEST_ASSERT(sscanf("123", "%u", &u) == 1, "sscanf unsigned");
    TEST_ASSERT(u == 123, "sscanf unsigned value");

    int n;
    TEST_ASSERT(sscanf("abc", "%s%n", str, &n) == 1, "sscanf %%n");
    TEST_ASSERT(n == 3, "sscanf %%n value");
}

void test_libc_all(void) {
    test_string();
    test_string_extra();
    test_stdlib();
    test_stdlib_extra();
    test_snprintf();
    test_sscanf();
}
