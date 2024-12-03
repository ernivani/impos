#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>
#include <string.h>

static filesystem_t fs;

void fs_initialize(void) {
    fs.file_count = 0;
    strcpy(fs.current_directory, "/");
    
    // Create root directory
    fs_create_file("/", 1);
}

int fs_create_file(const char* filename, uint8_t is_directory) {
    if (fs.file_count >= MAX_FILES) {
        return -1;
    }

    file_t* file = &fs.files[fs.file_count];
    strcpy(file->name, filename);
    file->size = 0;
    file->is_directory = is_directory;
    file->data = NULL;

    fs.file_count++;
    return 0;
}

int fs_write_file(const char* filename, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < fs.file_count; i++) {
        if (strcmp(fs.files[i].name, filename) == 0) {
            if (fs.files[i].is_directory) {
                return -1;
            }
            fs.files[i].data = (uint8_t*)data;
            fs.files[i].size = size;
            return 0;
        }
    }
    return -1;
}

int fs_read_file(const char* filename, uint8_t* buffer, size_t* size) {
    for (size_t i = 0; i < fs.file_count; i++) {
        if (strcmp(fs.files[i].name, filename) == 0) {
            if (fs.files[i].is_directory) {
                return -1;
            }
            memcpy(buffer, fs.files[i].data, fs.files[i].size);
            *size = fs.files[i].size;
            return 0;
        }
    }
    return -1;
}

void fs_list_directory(void) {
    for (size_t i = 0; i < fs.file_count; i++) {
        printf("%s %s\n", 
            fs.files[i].is_directory ? "DIR" : "FILE",
            fs.files[i].name);
    }
}

int fs_change_directory(const char* dirname) {
    return 0;
}