#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H

#include <stddef.h>
#include <stdint.h>

#define MAX_FILENAME 256
#define MAX_FILES 100
#define MAX_FILE_SIZE 4096

typedef struct {
    char name[MAX_FILENAME];
    uint8_t* data;
    size_t size;
    uint8_t is_directory;
} file_t;

typedef struct {
    file_t files[MAX_FILES];
    size_t file_count;
    char current_directory[MAX_FILENAME];
} filesystem_t;

void fs_initialize(void);
int fs_create_file(const char* filename, uint8_t is_directory);
int fs_write_file(const char* filename, const uint8_t* data, size_t size);
int fs_read_file(const char* filename, uint8_t* buffer, size_t* size);
int fs_delete_file(const char* filename);
void fs_list_directory(void);
int fs_change_directory(const char* dirname);

#endif