/*
 * devfs.c — Device Filesystem
 *
 * Dynamic device registration at /dev.  Device drivers call
 * devfs_register() to expose their read/write/ioctl entry points.
 * This replaces the hardcoded dev_read()/dev_write() dispatch in fs.c.
 *
 * Supports subdirectories (e.g. /dev/dri/card0).
 */

#include <kernel/vfs.h>
#include <kernel/fs.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

/* ── Device table ──────────────────────────────────────────────────── */

#define DEVFS_MAX_DEVICES  32
#define DEVFS_NAME_MAX     32

typedef int (*devfs_read_fn)(uint8_t *buf, size_t *size);
typedef int (*devfs_write_fn)(const uint8_t *data, size_t size);

typedef struct {
    char         name[DEVFS_NAME_MAX]; /* relative path, e.g. "null" or "dri/card0" */
    uint8_t      major;
    uint8_t      minor;
    devfs_read_fn  read;
    devfs_write_fn write;
    int          active;
} devfs_entry_t;

static devfs_entry_t dev_table[DEVFS_MAX_DEVICES];
static int num_devices = 0;

/* ── Built-in device implementations ──────────────────────────────── */

static int dev_null_read(uint8_t *buf, size_t *size)  { (void)buf; *size = 0; return 0; }
static int dev_null_write(const uint8_t *d, size_t s)  { (void)d; (void)s; return 0; }

static int dev_zero_read(uint8_t *buf, size_t *size) {
    size_t n = *size;
    if (n > 256) n = 256;
    memset(buf, 0, n);
    *size = n;
    return 0;
}
static int dev_zero_write(const uint8_t *d, size_t s) { (void)d; (void)s; return 0; }

static int dev_tty_read(uint8_t *buf, size_t *size) {
    extern char getchar(void);
    buf[0] = (uint8_t)getchar();
    *size = 1;
    return 0;
}
static int dev_tty_write(const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; i++)
        putchar((char)data[i]);
    return 0;
}

/* Forward declaration — defined in crypto module */
extern void prng_random(uint8_t *buf, size_t len);

static int dev_urandom_read(uint8_t *buf, size_t *size) {
    size_t n = *size;
    if (n > 256) n = 256;
    prng_random(buf, n);
    *size = n;
    return 0;
}
static int dev_urandom_write(const uint8_t *d, size_t s) { (void)d; (void)s; return 0; }

static int dev_drm_read(uint8_t *buf, size_t *size)   { (void)buf; *size = 0; return 0; }
static int dev_drm_write(const uint8_t *d, size_t s)   { (void)d; (void)s; return 0; }

/* ── Registration API ──────────────────────────────────────────────── */

int devfs_register(const char *name, uint8_t major, uint8_t minor,
                   devfs_read_fn read_fn, devfs_write_fn write_fn)
{
    if (!name || num_devices >= DEVFS_MAX_DEVICES) return -1;

    /* Find free slot */
    for (int i = 0; i < DEVFS_MAX_DEVICES; i++) {
        if (!dev_table[i].active) {
            strncpy(dev_table[i].name, name, DEVFS_NAME_MAX - 1);
            dev_table[i].name[DEVFS_NAME_MAX - 1] = '\0';
            dev_table[i].major = major;
            dev_table[i].minor = minor;
            dev_table[i].read  = read_fn;
            dev_table[i].write = write_fn;
            dev_table[i].active = 1;
            num_devices++;
            return 0;
        }
    }
    return -1;
}

static devfs_entry_t *devfs_find(const char *name) {
    for (int i = 0; i < DEVFS_MAX_DEVICES; i++) {
        if (dev_table[i].active && strcmp(dev_table[i].name, name) == 0)
            return &dev_table[i];
    }
    return NULL;
}

/* ── VFS ops ───────────────────────────────────────────────────────── */

static int devfs_read_file(void *priv, const char *path,
                           uint8_t *buf, size_t *size)
{
    (void)priv;
    if (!path || *path == '\0') return -1;
    if (*path == '/') path++;

    devfs_entry_t *dev = devfs_find(path);
    if (!dev || !dev->read) return -1;
    return dev->read(buf, size);
}

static int devfs_write_file(void *priv, const char *path,
                            const uint8_t *data, size_t size)
{
    (void)priv;
    if (!path || *path == '\0') return -1;
    if (*path == '/') path++;

    devfs_entry_t *dev = devfs_find(path);
    if (!dev || !dev->write) return -1;
    return dev->write(data, size);
}

/* Check if 'prefix' is a directory prefix of device paths.
 * E.g. "dri" is a prefix of "dri/card0". */
static int is_device_subdir(const char *prefix) {
    size_t plen = strlen(prefix);
    for (int i = 0; i < DEVFS_MAX_DEVICES; i++) {
        if (dev_table[i].active &&
            strncmp(dev_table[i].name, prefix, plen) == 0 &&
            dev_table[i].name[plen] == '/') {
            return 1;
        }
    }
    return 0;
}

static int devfs_readdir(void *priv, const char *path,
                         fs_dir_entry_info_t *out, int max)
{
    (void)priv;
    int count = 0;

    /* Normalize path */
    const char *dir = "";
    if (path && *path == '/') path++;
    if (path && *path != '\0') dir = path;
    size_t dlen = strlen(dir);

    /* Track subdirectories we've already listed to avoid duplicates */
    char seen_dirs[8][DEVFS_NAME_MAX];
    int seen_count = 0;

    for (int i = 0; i < DEVFS_MAX_DEVICES && count < max; i++) {
        if (!dev_table[i].active) continue;

        const char *name = dev_table[i].name;

        /* If we're listing a subdirectory, filter to matching prefix */
        if (dlen > 0) {
            if (strncmp(name, dir, dlen) != 0 || name[dlen] != '/')
                continue;
            name = name + dlen + 1; /* skip "dir/" */
        }

        /* Check if this is a direct child or nested */
        const char *slash = strchr(name, '/');
        if (slash) {
            /* This device is in a subdirectory — list the subdir as a dir entry */
            size_t sublen = (size_t)(slash - name);
            char subdir[DEVFS_NAME_MAX];
            if (sublen >= DEVFS_NAME_MAX) sublen = DEVFS_NAME_MAX - 1;
            memcpy(subdir, name, sublen);
            subdir[sublen] = '\0';

            /* Check if we've already listed this subdir */
            int already = 0;
            for (int s = 0; s < seen_count; s++) {
                if (strcmp(seen_dirs[s], subdir) == 0) { already = 1; break; }
            }
            if (already) continue;
            if (seen_count < 8) {
                strncpy(seen_dirs[seen_count], subdir, DEVFS_NAME_MAX - 1);
                seen_dirs[seen_count][DEVFS_NAME_MAX - 1] = '\0';
                seen_count++;
            }

            memset(&out[count], 0, sizeof(out[count]));
            strncpy(out[count].name, subdir, MAX_NAME_LEN - 1);
            out[count].type = INODE_DIR;
            out[count].inode = 0xD000 + i;
            count++;
        } else {
            /* Direct child device */
            memset(&out[count], 0, sizeof(out[count]));
            strncpy(out[count].name, name, MAX_NAME_LEN - 1);
            out[count].type = INODE_CHARDEV;
            out[count].inode = 0xC000 + i;
            count++;
        }
    }
    return count;
}

static int devfs_stat(void *priv, const char *path, inode_t *out)
{
    (void)priv;
    memset(out, 0, sizeof(*out));

    if (!path || *path == '\0' || strcmp(path, "/") == 0) {
        out->type = INODE_DIR;
        out->mode = 0755;
        return 0;
    }

    if (*path == '/') path++;

    /* Check for device */
    devfs_entry_t *dev = devfs_find(path);
    if (dev) {
        out->type = INODE_CHARDEV;
        out->mode = 0666;
        out->blocks[0] = dev->major;
        out->blocks[1] = dev->minor;
        return 0;
    }

    /* Check for subdirectory (e.g. "dri") */
    if (is_device_subdir(path)) {
        out->type = INODE_DIR;
        out->mode = 0755;
        return 0;
    }

    return -1;
}

static int devfs_mount_cb(void *priv)
{
    (void)priv;

    /* Register built-in devices */
    devfs_register("null",      DEV_MAJOR_NULL,    0, dev_null_read,    dev_null_write);
    devfs_register("zero",      DEV_MAJOR_ZERO,    0, dev_zero_read,    dev_zero_write);
    devfs_register("tty",       DEV_MAJOR_TTY,     0, dev_tty_read,     dev_tty_write);
    devfs_register("urandom",   DEV_MAJOR_URANDOM, 0, dev_urandom_read, dev_urandom_write);
    devfs_register("dri/card0", DEV_MAJOR_DRM,     0, dev_drm_read,     dev_drm_write);

    DBG("[DEVFS] Mounted at /dev (%d devices)", num_devices);
    return 0;
}

/* ── Public interface ──────────────────────────────────────────────── */

static vfs_ops_t devfs_ops = {
    .name       = "devfs",
    .mount      = devfs_mount_cb,
    .unmount    = NULL,
    .create     = NULL,
    .unlink     = NULL,
    .read_file  = devfs_read_file,
    .write_file = devfs_write_file,
    .read_at    = NULL,
    .write_at   = NULL,
    .readdir    = devfs_readdir,
    .stat       = devfs_stat,
    .chmod      = NULL,
    .chown      = NULL,
    .rename     = NULL,
    .truncate   = NULL,
    .symlink    = NULL,
    .readlink   = NULL,
    .sync       = NULL,
};

void devfs_init(void) {
    memset(dev_table, 0, sizeof(dev_table));
    num_devices = 0;
    vfs_mount("/dev", &devfs_ops, NULL);
}
