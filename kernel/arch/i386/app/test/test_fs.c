#include <kernel/test.h>
#include <kernel/fs.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <kernel/gfx.h>
#include <kernel/quota.h>
#include <kernel/mouse.h>
#include <kernel/vfs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

/* ---- FS v3 Tests (VFS, procfs, devfs, tmpfs, double-indirect, truncate) ---- */

static void test_fs_v3(void) {
    printf("== FS v3 Tests ==\n");

    const char* saved_user = user_get_current();
    user_set_current("root");

    /* --- VFS mount table --- */
    vfs_mount_t *mtab;
    int mcount;
    int ret = vfs_get_mounts(&mtab, &mcount);
    TEST_ASSERT(ret == 0, "vfs_get_mounts succeeds");
    TEST_ASSERT(mcount >= 3, "at least 3 VFS mounts (proc/dev/tmp)");

    int found_proc = 0, found_dev = 0, found_tmp = 0;
    for (int i = 0; i < mcount; i++) {
        if (!mtab[i].active) continue;
        if (strcmp(mtab[i].prefix, "/proc") == 0) found_proc = 1;
        if (strcmp(mtab[i].prefix, "/dev") == 0)  found_dev = 1;
        if (strcmp(mtab[i].prefix, "/tmp") == 0)  found_tmp = 1;
    }
    TEST_ASSERT(found_proc, "vfs: /proc mounted");
    TEST_ASSERT(found_dev,  "vfs: /dev mounted");
    TEST_ASSERT(found_tmp,  "vfs: /tmp mounted");

    /* --- VFS resolve --- */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve("/proc/uptime", &rel);
    TEST_ASSERT(mnt != NULL, "vfs_resolve /proc/uptime finds mount");
    TEST_ASSERT(strcmp(rel, "uptime") == 0, "vfs_resolve: relative is 'uptime'");

    mnt = vfs_resolve("/tmp/foo", &rel);
    TEST_ASSERT(mnt != NULL, "vfs_resolve /tmp/foo finds mount");
    TEST_ASSERT(strcmp(rel, "foo") == 0, "vfs_resolve: relative is 'foo'");

    /* Root path should NOT match any mount */
    mnt = vfs_resolve("/etc/passwd", &rel);
    TEST_ASSERT(mnt == NULL, "vfs_resolve /etc/passwd returns NULL (root fs)");

    /* --- procfs read --- */
    uint8_t pbuf[512];
    size_t psize;
    ret = fs_read_file("/proc/version", pbuf, &psize);
    TEST_ASSERT(ret == 0, "procfs: read /proc/version");
    TEST_ASSERT(psize > 0, "procfs: version has content");

    ret = fs_read_file("/proc/meminfo", pbuf, &psize);
    TEST_ASSERT(ret == 0, "procfs: read /proc/meminfo");
    TEST_ASSERT(psize > 0, "procfs: meminfo has content");

    ret = fs_read_file("/proc/uptime", pbuf, &psize);
    TEST_ASSERT(ret == 0, "procfs: read /proc/uptime");
    TEST_ASSERT(psize > 0, "procfs: uptime has content");

    /* procfs is read-only */
    ret = fs_create_file("/proc/hacked", 0);
    TEST_ASSERT(ret != 0, "procfs: create rejected (read-only)");

    ret = fs_write_file("/proc/version", (const uint8_t*)"x", 1);
    TEST_ASSERT(ret != 0, "procfs: write rejected (read-only)");

    /* --- tmpfs CRUD --- */
    ret = fs_create_file("/tmp/test_v3", 0);
    TEST_ASSERT(ret == 0, "tmpfs: create file");

    const char *tdata = "tmpfs works!";
    ret = fs_write_file("/tmp/test_v3", (const uint8_t*)tdata, strlen(tdata));
    TEST_ASSERT(ret == 0, "tmpfs: write file");

    uint8_t tbuf[256];
    size_t tsize;
    ret = fs_read_file("/tmp/test_v3", tbuf, &tsize);
    TEST_ASSERT(ret == 0, "tmpfs: read file");
    TEST_ASSERT(tsize == strlen(tdata), "tmpfs: read size matches");
    TEST_ASSERT(memcmp(tbuf, tdata, tsize) == 0, "tmpfs: data integrity");

    /* tmpfs mkdir + subfile */
    ret = fs_create_file("/tmp/test_dir_v3", 1);
    TEST_ASSERT(ret == 0, "tmpfs: mkdir");

    /* tmpfs delete */
    ret = fs_delete_file("/tmp/test_v3");
    TEST_ASSERT(ret == 0, "tmpfs: delete file");

    ret = fs_read_file("/tmp/test_v3", tbuf, &tsize);
    TEST_ASSERT(ret != 0, "tmpfs: deleted file unreadable");

    ret = fs_delete_file("/tmp/test_dir_v3");
    TEST_ASSERT(ret == 0, "tmpfs: rmdir");

    /* --- fs_write_at / fs_read_at (partial I/O) --- */
    ret = fs_create_file("/test_write_at", 0);
    TEST_ASSERT(ret == 0, "write_at: create file");

    /* Resolve inode for the file */
    uint32_t parent;
    char fname[MAX_NAME_LEN];
    int ino = fs_resolve_path("/test_write_at", &parent, fname);
    TEST_ASSERT(ino >= 0, "write_at: resolve path");

    if (ino >= 0) {
        /* Write at offset 0 */
        const char *part1 = "AAAA";
        ret = fs_write_at((uint32_t)ino, (const uint8_t*)part1, 0, 4);
        TEST_ASSERT(ret == 4, "write_at: wrote 4 bytes at offset 0");

        /* Write at offset 8 (creates a hole from 4-7) */
        const char *part2 = "BBBB";
        ret = fs_write_at((uint32_t)ino, (const uint8_t*)part2, 8, 4);
        TEST_ASSERT(ret == 4, "write_at: wrote 4 bytes at offset 8");

        /* Read back the whole thing */
        uint8_t rbuf[16];
        memset(rbuf, 0xFF, sizeof(rbuf));
        ret = fs_read_at((uint32_t)ino, rbuf, 0, 12);
        TEST_ASSERT(ret == 12, "write_at: read 12 bytes");
        TEST_ASSERT(memcmp(rbuf, "AAAA", 4) == 0, "write_at: first chunk intact");
        TEST_ASSERT(memcmp(rbuf + 8, "BBBB", 4) == 0, "write_at: second chunk intact");
        /* Hole bytes (4-7) should be zero */
        int hole_ok = (rbuf[4] == 0 && rbuf[5] == 0 && rbuf[6] == 0 && rbuf[7] == 0);
        TEST_ASSERT(hole_ok, "write_at: hole filled with zeros");
    }
    fs_delete_file("/test_write_at");

    /* --- fs_truncate --- */
    ret = fs_create_file("/test_trunc", 0);
    TEST_ASSERT(ret == 0, "truncate: create file");

    /* Write some data */
    const char *trdata = "0123456789ABCDEF";
    fs_write_file("/test_trunc", (const uint8_t*)trdata, 16);

    /* Truncate to 8 bytes */
    ret = fs_truncate("/test_trunc", 8);
    TEST_ASSERT(ret == 0, "truncate: shrink to 8");

    uint8_t trbuf[32];
    size_t trsize;
    ret = fs_read_file("/test_trunc", trbuf, &trsize);
    TEST_ASSERT(ret == 0, "truncate: read after shrink");
    TEST_ASSERT(trsize == 8, "truncate: size is 8");
    TEST_ASSERT(memcmp(trbuf, "01234567", 8) == 0, "truncate: data intact");

    /* Extend to 16 (new bytes should be zero) */
    ret = fs_truncate("/test_trunc", 16);
    TEST_ASSERT(ret == 0, "truncate: extend to 16");

    ret = fs_read_file("/test_trunc", trbuf, &trsize);
    TEST_ASSERT(ret == 0, "truncate: read after extend");
    TEST_ASSERT(trsize == 16, "truncate: size is 16");
    int ext_ok = 1;
    for (int i = 8; i < 16; i++) {
        if (trbuf[i] != 0) { ext_ok = 0; break; }
    }
    TEST_ASSERT(ext_ok, "truncate: extended bytes are zero");

    fs_delete_file("/test_trunc");

    /* --- Double-indirect large file test (> 32KB = beyond direct blocks) --- */
    size_t big_size = 40960;  /* 40KB: needs indirect blocks */
    uint8_t *wbuf = (uint8_t*)malloc(big_size);
    TEST_ASSERT(wbuf != NULL, "big file: malloc write buf");
    if (wbuf) {
        for (size_t i = 0; i < big_size; i++)
            wbuf[i] = (uint8_t)((i * 7 + 13) & 0xFF);

        ret = fs_create_file("/test_bigfile", 0);
        TEST_ASSERT(ret == 0, "big file: create");

        ret = fs_write_file("/test_bigfile", wbuf, big_size);
        TEST_ASSERT(ret == 0, "big file: write 40KB");

        uint8_t *rbig = (uint8_t*)malloc(big_size);
        TEST_ASSERT(rbig != NULL, "big file: malloc read buf");
        if (rbig) {
            size_t rbs;
            ret = fs_read_file("/test_bigfile", rbig, &rbs);
            TEST_ASSERT(ret == 0, "big file: read back");
            TEST_ASSERT(rbs == big_size, "big file: size matches");

            int data_ok = 1;
            for (size_t i = 0; i < big_size; i++) {
                if (rbig[i] != (uint8_t)((i * 7 + 13) & 0xFF)) {
                    data_ok = 0;
                    break;
                }
            }
            TEST_ASSERT(data_ok, "big file: 40KB data integrity");
            free(rbig);
        }
        fs_delete_file("/test_bigfile");
        free(wbuf);
    }

    /* --- FS geometry constants --- */
    TEST_ASSERT(NUM_BLOCKS == 65536, "geometry: 65536 blocks");
    TEST_ASSERT(NUM_INODES == 4096, "geometry: 4096 inodes");
    TEST_ASSERT(BLOCK_SIZE == 4096, "geometry: 4KB blocks");
    TEST_ASSERT(DIRECT_BLOCKS == 8, "geometry: 8 direct blocks");

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

/* ---- Mouse Tests ---- */

static void test_mouse(void) {
    printf("== Mouse Tests ==\n");

    /* Mouse should be initialized */
    TEST_ASSERT(mouse_get_x() >= 0, "mouse x >= 0");
    TEST_ASSERT(mouse_get_y() >= 0, "mouse y >= 0");
    TEST_ASSERT(mouse_get_buttons() == 0, "mouse buttons init 0");
}

void test_fs_all(void) {
    test_fs();
    test_fs_indirect();
    test_fs_v3();
    test_user();
    test_gfx();
    test_quota();
    test_mouse();
}
