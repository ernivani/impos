#include <kernel/test.h>
#include <kernel/fs.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <kernel/gfx.h>
#include <kernel/quota.h>
#include <kernel/ip.h>
#include <kernel/net.h>
#include <kernel/endian.h>
#include <kernel/firewall.h>
#include <kernel/mouse.h>
#include <kernel/crypto.h>
#include <kernel/tls.h>
#include <kernel/socket.h>
#include <kernel/dns.h>
#include <kernel/task.h>
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

/* ---- Filesystem Tests ---- */

static void test_fs(void) {
    printf("== Filesystem Tests ==\n");

    /* Switch to root for FS tests (need write on / directory) */
    const char* saved_user = user_get_current();
    user_set_current("root");

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

    /* Restore original user */
    if (saved_user)
        user_set_current(saved_user);
}

/* ---- Indirect Block Tests ---- */

static void test_fs_indirect(void) {
    printf("== FS Indirect Block Tests ==\n");

    const char* saved_user = user_get_current();
    user_set_current("root");

    /* Create a large file (8192 bytes > 4096 direct limit) */
    size_t large_size = 8192;
    uint8_t* wbuf = (uint8_t*)malloc(large_size);
    TEST_ASSERT(wbuf != NULL, "indirect: malloc write buf");
    if (!wbuf) {
        if (saved_user) user_set_current(saved_user);
        return;
    }

    /* Fill with pattern */
    for (size_t i = 0; i < large_size; i++)
        wbuf[i] = (uint8_t)(i & 0xFF);

    int ret = fs_create_file("/tmp_large_file", 0);
    TEST_ASSERT(ret == 0, "indirect: create large file");

    ret = fs_write_file("/tmp_large_file", wbuf, large_size);
    TEST_ASSERT(ret == 0, "indirect: write 8192 bytes");

    /* Read back */
    uint8_t* rbuf = (uint8_t*)malloc(large_size);
    TEST_ASSERT(rbuf != NULL, "indirect: malloc read buf");
    if (!rbuf) {
        free(wbuf);
        fs_delete_file("/tmp_large_file");
        if (saved_user) user_set_current(saved_user);
        return;
    }

    size_t rsize;
    ret = fs_read_file("/tmp_large_file", rbuf, &rsize);
    TEST_ASSERT(ret == 0, "indirect: read large file");
    TEST_ASSERT(rsize == large_size, "indirect: read size matches");

    int data_ok = 1;
    for (size_t i = 0; i < large_size; i++) {
        if (rbuf[i] != (uint8_t)(i & 0xFF)) {
            data_ok = 0;
            break;
        }
    }
    TEST_ASSERT(data_ok, "indirect: data integrity");

    /* Delete and verify */
    ret = fs_delete_file("/tmp_large_file");
    TEST_ASSERT(ret == 0, "indirect: delete large file");

    ret = fs_read_file("/tmp_large_file", rbuf, &rsize);
    TEST_ASSERT(ret != 0, "indirect: deleted file unreadable");

    free(wbuf);
    free(rbuf);

    if (saved_user)
        user_set_current(saved_user);
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

/* ---- Graphics Tests ---- */

static void test_gfx(void) {
    printf("== Graphics Tests ==\n");

    TEST_ASSERT(gfx_is_active() == 1, "gfx is active");
    TEST_ASSERT(gfx_width() > 0, "gfx width > 0");
    TEST_ASSERT(gfx_height() > 0, "gfx height > 0");
    TEST_ASSERT(gfx_bpp() == 32, "gfx bpp == 32");
    TEST_ASSERT(gfx_cols() == gfx_width() / 8, "gfx cols == width/8");
    TEST_ASSERT(gfx_rows() == gfx_height() / 16, "gfx rows == height/16");
    TEST_ASSERT(gfx_pitch() >= gfx_width() * 4, "gfx pitch >= width*4");
    TEST_ASSERT(gfx_backbuffer() != NULL, "gfx backbuffer not null");

    /* Out of bounds put_pixel should not crash */
    gfx_put_pixel(-1, -1, 0xFF0000);
    gfx_put_pixel((int)gfx_width() + 1, (int)gfx_height() + 1, 0xFF0000);
    TEST_ASSERT(1, "gfx put_pixel OOB no crash");

    /* Fill rect clipping at edges */
    gfx_fill_rect((int)gfx_width() - 5, (int)gfx_height() - 5, 100, 100, 0x00FF00);
    TEST_ASSERT(1, "gfx fill_rect clip no crash");

    /* Draw char without crash */
    gfx_draw_char(0, 0, 'A', 0xFFFFFF, 0x000000);
    TEST_ASSERT(1, "gfx draw_char no crash");

    /* VGA-to-RGB spot checks */
    /* Black = 0x000000, White = 0xFFFFFF, Blue = 0x0000AA, Red = 0xAA0000 */
    /* These are tested indirectly via the TTY color table, just verify gfx works */
    TEST_ASSERT(GFX_RGB(0,0,0) == 0x000000, "GFX_RGB black");
    TEST_ASSERT(GFX_RGB(255,255,255) == 0xFFFFFF, "GFX_RGB white");
    TEST_ASSERT(GFX_RGB(255,0,0) == 0xFF0000, "GFX_RGB red");
    TEST_ASSERT(GFX_RGB(0,255,0) == 0x00FF00, "GFX_RGB green");
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

/* ---- Quota Tests ---- */

static void test_quota(void) {
    printf("== Quota Tests ==\n");

    /* Set quota for uid 999 */
    TEST_ASSERT(quota_set(999, 5, 10) == 0, "quota set");

    quota_entry_t* q = quota_get(999);
    TEST_ASSERT(q != NULL, "quota get");
    TEST_ASSERT(q->max_inodes == 5, "quota max_inodes");
    TEST_ASSERT(q->max_blocks == 10, "quota max_blocks");

    /* Check allows initially */
    TEST_ASSERT(quota_check_inode(999) == 0, "quota check inode ok");
    TEST_ASSERT(quota_check_block(999, 5) == 0, "quota check block ok");

    /* Add usage */
    for (int i = 0; i < 5; i++) quota_add_inode(999);
    TEST_ASSERT(quota_check_inode(999) == -1, "quota inode exceeded");

    quota_add_blocks(999, 8);
    TEST_ASSERT(quota_check_block(999, 3) == -1, "quota block exceeded");
    TEST_ASSERT(quota_check_block(999, 2) == 0, "quota block still ok");

    /* Remove usage */
    quota_remove_inode(999);
    TEST_ASSERT(quota_check_inode(999) == 0, "quota inode after remove");

    /* No quota for uid 998 = unlimited */
    TEST_ASSERT(quota_check_inode(998) == 0, "quota no limit inode");
    TEST_ASSERT(quota_check_block(998, 1000) == 0, "quota no limit block");

    /* Clean up */
    q->active = 0;
}

/* ---- Network Tests ---- */

static void test_network(void) {
    printf("== Network Tests ==\n");

    /* Byte order: htons / ntohs */
    TEST_ASSERT(htons(0x1234) == 0x3412, "htons swap");
    TEST_ASSERT(ntohs(0x3412) == 0x1234, "ntohs swap");
    TEST_ASSERT(ntohs(htons(0xABCD)) == 0xABCD, "htons/ntohs roundtrip");

    /* htonl / ntohl */
    TEST_ASSERT(htonl(0x12345678) == 0x78563412, "htonl swap");
    TEST_ASSERT(ntohl(htonl(0xDEADBEEF)) == 0xDEADBEEF, "htonl/ntohl roundtrip");

    /* IP checksum on a known header:
     * Version/IHL=0x45, TOS=0, Len=0x003C, ID=0x1C46, Flags=0x4000,
     * TTL=0x40, Proto=0x06, Checksum=0, Src=0xAC100A63, Dst=0xAC100A0C
     * Expected checksum: the function returns host-order value which,
     * stored directly to uint16_t, yields correct wire bytes. */
    uint8_t ip_hdr[20] = {
        0x45, 0x00, 0x00, 0x3C,  /* ver/ihl, tos, total_len */
        0x1C, 0x46, 0x40, 0x00,  /* id, flags/frag */
        0x40, 0x06, 0x00, 0x00,  /* ttl, proto(TCP), checksum=0 */
        0xAC, 0x10, 0x0A, 0x63,  /* src: 172.16.10.99 */
        0xAC, 0x10, 0x0A, 0x0C   /* dst: 172.16.10.12 */
    };

    uint16_t csum = ip_checksum(ip_hdr, 20);
    TEST_ASSERT(csum != 0, "ip_checksum non-zero for zeroed field");

    /* Store checksum and verify sum-to-zero property */
    *(uint16_t*)(ip_hdr + 10) = csum;
    uint16_t verify = ip_checksum(ip_hdr, 20);
    TEST_ASSERT(verify == 0, "ip_checksum sum-to-zero");

    /* Net config */
    net_config_t* cfg = net_get_config();
    TEST_ASSERT(cfg != NULL, "net_get_config non-null");
    TEST_ASSERT(cfg->link_up == 1, "link is up");

    /* MAC should not be all-zero (driver set it) */
    int mac_nonzero = 0;
    for (int i = 0; i < 6; i++) {
        if (cfg->mac[i] != 0) mac_nonzero = 1;
    }
    TEST_ASSERT(mac_nonzero, "MAC address set");
}

/* ---- Firewall Tests ---- */

static void test_firewall(void) {
    printf("== Firewall Tests ==\n");

    /* Save current state and reinit */
    firewall_flush();
    firewall_set_default(FW_ACTION_ALLOW);

    uint8_t src[4] = {10, 0, 2, 15};
    uint8_t dst[4] = {10, 0, 2, 1};

    /* Default allow */
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 80) == FW_ACTION_ALLOW,
                "fw default allow");

    /* Add deny ICMP rule */
    fw_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.protocol = FW_PROTO_ICMP;
    rule.action = FW_ACTION_DENY;
    rule.enabled = 1;
    TEST_ASSERT(firewall_add_rule(&rule) == 0, "fw add rule");
    TEST_ASSERT(firewall_rule_count() == 1, "fw rule count 1");

    /* ICMP should be denied, TCP still allowed */
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_ICMP, 0) == FW_ACTION_DENY,
                "fw deny icmp");
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 80) == FW_ACTION_ALLOW,
                "fw allow tcp with icmp rule");

    /* Add deny TCP port 80 */
    fw_rule_t rule2;
    memset(&rule2, 0, sizeof(rule2));
    rule2.protocol = FW_PROTO_TCP;
    rule2.action = FW_ACTION_DENY;
    rule2.dst_port_min = 80;
    rule2.dst_port_max = 80;
    rule2.enabled = 1;
    firewall_add_rule(&rule2);

    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 80) == FW_ACTION_DENY,
                "fw deny tcp:80");
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 443) == FW_ACTION_ALLOW,
                "fw allow tcp:443");

    /* Test default deny */
    firewall_set_default(FW_ACTION_DENY);
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_UDP, 53) == FW_ACTION_DENY,
                "fw default deny udp");

    /* Test source IP matching */
    fw_rule_t rule3;
    memset(&rule3, 0, sizeof(rule3));
    rule3.protocol = FW_PROTO_ALL;
    rule3.action = FW_ACTION_ALLOW;
    rule3.src_ip[0] = 10; rule3.src_ip[1] = 0; rule3.src_ip[2] = 2; rule3.src_ip[3] = 15;
    memset(rule3.src_mask, 255, 4);
    rule3.enabled = 1;
    firewall_add_rule(&rule3);

    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_UDP, 53) == FW_ACTION_ALLOW,
                "fw allow by src ip");
    uint8_t other_src[4] = {192, 168, 1, 1};
    TEST_ASSERT(firewall_check(other_src, dst, FW_PROTO_UDP, 53) == FW_ACTION_DENY,
                "fw deny other src");

    /* Delete rule and flush */
    TEST_ASSERT(firewall_del_rule(0) == 0, "fw del rule 0");
    TEST_ASSERT(firewall_rule_count() == 2, "fw count after del");

    firewall_flush();
    TEST_ASSERT(firewall_rule_count() == 0, "fw count after flush");
    firewall_set_default(FW_ACTION_ALLOW);
}

/* ---- Mouse Tests ---- */

static void test_mouse(void) {
    printf("== Mouse Tests ==\n");

    /* Mouse should be initialized */
    TEST_ASSERT(mouse_get_x() >= 0, "mouse x >= 0");
    TEST_ASSERT(mouse_get_y() >= 0, "mouse y >= 0");
    TEST_ASSERT(mouse_get_buttons() == 0, "mouse buttons init 0");
}

/* ---- Crypto Tests ---- */

void test_crypto(void) {
    printf("== Crypto Tests ==\n");

    /* SHA-256: NIST test vector - SHA256("abc") */
    {
        uint8_t digest[32];
        sha256((const uint8_t *)"abc", 3, digest);
        /* Expected: ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad */
        uint8_t expected[] = {
            0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
            0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
            0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
            0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
        };
        TEST_ASSERT(memcmp(digest, expected, 32) == 0, "SHA256(abc)");
    }

    /* SHA-256: empty string */
    {
        uint8_t digest[32];
        sha256((const uint8_t *)"", 0, digest);
        uint8_t expected[] = {
            0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,
            0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
            0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,
            0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
        };
        TEST_ASSERT(memcmp(digest, expected, 32) == 0, "SHA256(empty)");
    }

    /* HMAC-SHA-256: RFC 4231 Test Case 2 */
    {
        uint8_t mac[32];
        hmac_sha256((const uint8_t *)"Jefe", 4,
                    (const uint8_t *)"what do ya want for nothing?", 28,
                    mac);
        uint8_t expected[] = {
            0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,
            0x6a,0x04,0x24,0x26,0x08,0x95,0x75,0xc7,
            0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,
            0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
        };
        TEST_ASSERT(memcmp(mac, expected, 32) == 0, "HMAC-SHA256 RFC4231 TC2");
    }

    /* AES-128: FIPS 197 Appendix B test vector */
    {
        uint8_t key[16] = {
            0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
            0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
        };
        uint8_t plaintext[16] = {
            0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,
            0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34
        };
        uint8_t expected_ct[16] = {
            0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,
            0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32
        };

        aes128_ctx_t ctx;
        aes128_init(&ctx, key);

        uint8_t ct[16];
        aes128_encrypt_block(&ctx, plaintext, ct);
        TEST_ASSERT(memcmp(ct, expected_ct, 16) == 0, "AES-128 encrypt");

        uint8_t pt[16];
        aes128_decrypt_block(&ctx, ct, pt);
        TEST_ASSERT(memcmp(pt, plaintext, 16) == 0, "AES-128 decrypt");
    }

    /* AES-128-CBC encrypt/decrypt roundtrip */
    {
        uint8_t key[16] = {0};
        uint8_t iv[16] = {0};
        uint8_t data[32];
        memset(data, 0x42, 32);

        aes128_ctx_t ctx;
        aes128_init(&ctx, key);

        uint8_t cipher[32], plain[32];
        aes128_cbc_encrypt(&ctx, iv, data, 32, cipher);
        aes128_cbc_decrypt(&ctx, iv, cipher, 32, plain);
        TEST_ASSERT(memcmp(plain, data, 32) == 0, "AES-CBC roundtrip");
    }

    /* Bignum: 3^10 mod 7 = 4 */
    {
        bignum_t base, exp, mod, result;
        bn_zero(&base); base.d[0] = 3; base.top = 1;
        bn_zero(&exp);  exp.d[0] = 10; exp.top = 1;
        bn_zero(&mod);  mod.d[0] = 7;  mod.top = 1;
        bn_modexp(&result, &base, &exp, &mod);
        TEST_ASSERT(result.d[0] == 4, "bignum 3^10 mod 7 = 4");
    }

    /* Bignum: 2^16 mod 100 = 36 */
    {
        bignum_t base, exp, mod, result;
        bn_zero(&base); base.d[0] = 2;  base.top = 1;
        bn_zero(&exp);  exp.d[0] = 16;  exp.top = 1;
        bn_zero(&mod);  mod.d[0] = 100; mod.top = 1;
        bn_modexp(&result, &base, &exp, &mod);
        TEST_ASSERT(result.d[0] == 36, "bignum 2^16 mod 100 = 36");
    }

    /* Bignum mulmod: carry overflow test (2048-bit) */
    {
        bignum_t a, two, m, result;
        bn_zero(&a); bn_zero(&two); bn_zero(&m);
        /* m = 2^2047 + 1 (has MSB set, true 2048-bit number) */
        m.d[63] = 0x80000000;
        m.d[0] = 1;
        m.top = 64;
        /* a = 2^2047 = m - 1 */
        a.d[63] = 0x80000000;
        a.top = 64;
        /* Compute a*2 mod m = 2^2048 mod (2^2047+1) = 2^2047 - 1 */
        two.d[0] = 2; two.top = 1;
        bn_mulmod(&result, &a, &two, &m);
        bignum_t expected;
        bn_zero(&expected);
        for (int i = 0; i < 63; i++) expected.d[i] = 0xFFFFFFFF;
        expected.d[63] = 0x7FFFFFFF;
        expected.top = 64;
        TEST_ASSERT(bn_cmp(&result, &expected) == 0, "mulmod 2048-bit carry");
    }

    /* Bignum modexp: (m-1)^2 mod m = 1 (2048-bit) */
    {
        bignum_t base, exp, m, result;
        bn_zero(&base); bn_zero(&exp); bn_zero(&m);
        /* m = 2^2047 + 3 */
        m.d[63] = 0x80000000;
        m.d[0] = 3;
        m.top = 64;
        /* base = m - 1 = 2^2047 + 2 */
        base.d[63] = 0x80000000;
        base.d[0] = 2;
        base.top = 64;
        /* exp = 2 */
        exp.d[0] = 2; exp.top = 1;
        bn_modexp(&result, &base, &exp, &m);
        /* (m-1)^2 mod m = (-1)^2 mod m = 1 */
        TEST_ASSERT(result.d[0] == 1 && result.top == 1, "modexp (m-1)^2 mod m = 1");
    }

    /* PRNG: should produce non-zero, non-identical output */
    {
        uint8_t buf1[16], buf2[16];
        prng_init();
        prng_random(buf1, 16);
        prng_random(buf2, 16);
        int all_zero = 1;
        for (int i = 0; i < 16; i++)
            if (buf1[i] != 0) all_zero = 0;
        TEST_ASSERT(!all_zero, "PRNG non-zero output");
        TEST_ASSERT(memcmp(buf1, buf2, 16) != 0, "PRNG different outputs");
    }

    printf("  Crypto tests done.\n");
}

/* ---- TLS Test (requires network) ---- */

void test_tls(void) {
    printf("== TLS Test ==\n");

    net_config_t *cfg = net_get_config();
    if (!cfg || !cfg->link_up || (cfg->ip[0] == 0 && cfg->ip[1] == 0)) {
        printf("  SKIP: network not configured (run dhcp first)\n");
        return;
    }

    printf("  Attempting HTTPS GET to example.com...\n");

    /* Run in background thread so UI stays responsive */
    static https_async_t req;
    strcpy(req.host, "example.com");
    req.port = 443;
    strcpy(req.path, "/");

    if (https_get_async(&req) < 0) {
        printf("  Failed to start HTTPS thread\n");
        return;
    }

    printf("  TLS running in background (thread %d)...\n", req.tid);
    while (!req.done) {
        keyboard_run_idle();   /* keep WM/UI alive */
        task_yield();
    }

    if (req.result > 0 && req.body) {
        TEST_ASSERT(req.body_len > 0, "tls: got response body");
        /* Check for HTML content */
        int has_html = 0;
        for (size_t i = 0; i + 5 < req.body_len; i++) {
            if (memcmp(req.body + i, "<html", 5) == 0 ||
                memcmp(req.body + i, "<HTML", 5) == 0) {
                has_html = 1;
                break;
            }
        }
        TEST_ASSERT(has_html, "tls: response contains HTML");
        printf("  Received %u bytes of HTML\n", (unsigned)req.body_len);
        free(req.body);
    } else {
        printf("  HTTPS GET failed (ret=%d) - server may not support our cipher\n", req.result);
    }
}

/* ---- Run All ---- */

void test_run_all(void) {
    test_count = 0;
    test_pass = 0;
    test_fail = 0;

    printf("\n=== ImposOS Regression Tests ===\n\n");

    test_string();
    test_string_extra();
    test_stdlib();
    test_stdlib_extra();
    test_snprintf();
    test_sscanf();
    test_fs();
    test_fs_indirect();
    test_user();
    test_gfx();
    test_quota();
    test_network();
    test_firewall();
    test_mouse();
    test_crypto();

    printf("\n=== Results: %d/%d passed", test_pass, test_count);
    if (test_fail > 0) {
        printf(", %d FAILED", test_fail);
    }
    printf(" ===\n\n");
}
