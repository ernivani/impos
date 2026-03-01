#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(__is_libk)
#include <kernel/fs.h>
#endif

#define FILE_BUF_SIZE 4096

/* FILE structure */
struct _FILE {
    char path[128];
    int mode;        /* 0=read, 1=write, 2=append */
    uint8_t* buf;
    size_t buf_size;
    size_t buf_pos;  /* current read/write position */
    size_t buf_len;  /* amount of data in buffer */
    int eof;
    int error;
    int is_std;      /* 1=stdin, 2=stdout, 3=stderr */
};

static FILE _stdin_file  = { .is_std = 1 };
static FILE _stdout_file = { .is_std = 2 };
static FILE _stderr_file = { .is_std = 3 };

FILE* stdin  = &_stdin_file;
FILE* stdout = &_stdout_file;
FILE* stderr = &_stderr_file;

FILE* fopen(const char* path, const char* mode) {
#if defined(__is_libk)
    FILE* f = (FILE*)malloc(sizeof(FILE));
    if (!f) return NULL;
    memset(f, 0, sizeof(FILE));

    strncpy(f->path, path, sizeof(f->path) - 1);
    f->is_std = 0;

    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        f->mode = 0;
        /* Resolve inode to get actual file size (avoid malloc(MAX_FILE_SIZE) crash) */
        uint32_t parent;
        char fname[MAX_NAME_LEN];
        int ino = fs_resolve_path(path, &parent, fname);
        if (ino < 0) { free(f); return NULL; }
        inode_t inode;
        if (fs_read_inode((uint32_t)ino, &inode) != 0) { free(f); return NULL; }
        size_t alloc_size = (inode.type == INODE_CHARDEV) ? 4096 : (inode.size + 1);
        if (alloc_size < 1) alloc_size = 1;
        f->buf = (uint8_t*)malloc(alloc_size);
        if (!f->buf) { free(f); return NULL; }
        f->buf_size = alloc_size;
        size_t size;
        if (fs_read_file(path, f->buf, &size) != 0) {
            free(f->buf);
            free(f);
            return NULL;
        }
        f->buf_len = size;
        f->buf_pos = 0;
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
        f->mode = 1;
        f->buf = (uint8_t*)malloc(FILE_BUF_SIZE);
        if (!f->buf) { free(f); return NULL; }
        f->buf_size = FILE_BUF_SIZE;
        f->buf_len = 0;
        f->buf_pos = 0;
    } else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
        f->mode = 2;
        /* Resolve inode to get actual file size */
        uint32_t parent;
        char fname[MAX_NAME_LEN];
        int ino = fs_resolve_path(path, &parent, fname);
        size_t alloc_size = FILE_BUF_SIZE;
        if (ino >= 0) {
            inode_t inode;
            if (fs_read_inode((uint32_t)ino, &inode) == 0 && inode.size > 0)
                alloc_size = inode.size + FILE_BUF_SIZE; /* room to append */
        }
        f->buf = (uint8_t*)malloc(alloc_size);
        if (!f->buf) { free(f); return NULL; }
        f->buf_size = alloc_size;
        size_t size;
        if (fs_read_file(path, f->buf, &size) == 0) {
            f->buf_len = size;
            f->buf_pos = size;
        } else {
            f->buf_len = 0;
            f->buf_pos = 0;
        }
    } else {
        free(f);
        return NULL;
    }

    return f;
#else
    (void)path; (void)mode;
    return NULL;
#endif
}

int fclose(FILE* f) {
    if (!f || f->is_std) return 0;

#if defined(__is_libk)
    /* Flush write buffer */
    if (f->mode == 1 || f->mode == 2) {
        fs_write_file(f->path, f->buf, f->buf_len);
    }
#endif

    if (f->buf) free(f->buf);
    free(f);
    return 0;
}

int fgetc(FILE* f) {
    if (!f) return EOF;

    if (f->is_std == 1) {
        /* stdin */
        return (int)(unsigned char)getchar();
    }

    if (f->buf_pos >= f->buf_len) {
        f->eof = 1;
        return EOF;
    }

    return (int)f->buf[f->buf_pos++];
}

int fputc(int c, FILE* f) {
    if (!f) return EOF;

    if (f->is_std == 2 || f->is_std == 3) {
        /* stdout/stderr */
        putchar(c);
        return c;
    }

    if (!f->buf || f->buf_len >= f->buf_size)
        return EOF;

    f->buf[f->buf_len++] = (uint8_t)c;
    f->buf_pos = f->buf_len;
    return c;
}

size_t fread(void* ptr, size_t size, size_t count, FILE* f) {
    if (!f || !ptr || size == 0 || count == 0) return 0;

    size_t total = size * count;
    size_t nread = 0;
    uint8_t* dst = (uint8_t*)ptr;

    while (nread < total) {
        int c = fgetc(f);
        if (c == EOF) break;
        dst[nread++] = (uint8_t)c;
    }

    return nread / size;
}

size_t fwrite(const void* ptr, size_t size, size_t count, FILE* f) {
    if (!f || !ptr || size == 0 || count == 0) return 0;

    size_t total = size * count;
    size_t nwritten = 0;
    const uint8_t* src = (const uint8_t*)ptr;

    while (nwritten < total) {
        if (fputc(src[nwritten], f) == EOF) break;
        nwritten++;
    }

    return nwritten / size;
}

int fflush(FILE* f) {
    if (!f) return EOF;

    if (f->is_std == 2 || f->is_std == 3) {
        /* stdout/stderr: nothing to flush in our bare-metal environment */
        return 0;
    }

#if defined(__is_libk)
    if ((f->mode == 1 || f->mode == 2) && f->buf && f->buf_len > 0) {
        fs_write_file(f->path, f->buf, f->buf_len);
    }
#endif

    return 0;
}

int feof(FILE* f) {
    if (!f) return 1;
    return f->eof;
}

int ferror(FILE* f) {
    if (!f) return 1;
    return f->error;
}

int fputs(const char* s, FILE* f) {
    if (!f || !s) return EOF;
    while (*s) {
        if (fputc(*s, f) == EOF) return EOF;
        s++;
    }
    return 0;
}

char* fgets(char* s, int size, FILE* f) {
    if (!f || !s || size <= 0) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(f);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fseek(FILE* f, long offset, int whence) {
    if (!f || f->is_std) return -1;

    long new_pos;
    switch (whence) {
        case 0: /* SEEK_SET */
            new_pos = offset;
            break;
        case 1: /* SEEK_CUR */
            new_pos = (long)f->buf_pos + offset;
            break;
        case 2: /* SEEK_END */
            new_pos = (long)f->buf_len + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0) new_pos = 0;
    if ((size_t)new_pos > f->buf_len) new_pos = (long)f->buf_len;

    f->buf_pos = (size_t)new_pos;
    f->eof = 0;
    return 0;
}

long ftell(FILE* f) {
    if (!f || f->is_std) return -1;
    return (long)f->buf_pos;
}

void rewind(FILE* f) {
    if (!f) return;
    if (f->is_std) return;
    f->buf_pos = 0;
    f->eof = 0;
    f->error = 0;
}

int ungetc(int c, FILE* f) {
    if (!f || c == EOF) return EOF;
    if (f->is_std) return EOF;
    if (f->buf_pos > 0) {
        f->buf_pos--;
        f->buf[f->buf_pos] = (uint8_t)c;
        f->eof = 0;
        return c;
    }
    return EOF;
}
